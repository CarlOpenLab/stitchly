// 主流水线：固定区检测 -> 逐帧对齐决策 -> 合成
#include "internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ls {

namespace {

struct Decision {
    bool  kept    = false;
    bool  dropped = false; // 重复帧 / 无新内容
    bool  gap     = false; // 未能置信对齐
    float overlap = 0.f;   // 与上一保留帧重叠的行数（灰度图坐标）
    float score   = 1.f;
};

void copy_frame_rows(const LsImage& src, int y0, int y1,
                     uint8_t* dst_base, int dst_y, int width) {
    const size_t sstride = src.stride > 0 ? (size_t)src.stride : (size_t)src.width * 4;
    const size_t rowb = (size_t)width * 4;
    for (int y = y0; y < y1; y++) {
        std::memcpy(dst_base + (size_t)dst_y * rowb, src.data + (size_t)y * sstride, rowb);
        dst_y++;
    }
}

bool validate_frame(const LsImage& f, const LsPlan& plan) {
    return f.data && f.width == plan.full_w && f.height == plan.full_h;
}

} // namespace

void detect_static_rows(const std::vector<GrayImage>& gs,
                        int* top_s, int* bottom_s, int max_ratio_pct) {
    *top_s = 0;
    *bottom_s = 0;
    const size_t n = gs.size();
    if (n < 2) return;
    const int w = gs[0].w, h = gs[0].h;
    for (const GrayImage& g : gs)
        if (g.w != w || g.h != h) return;

    // 相邻帧对的逐行平均差
    std::vector<std::vector<double>> diff(n - 1, std::vector<double>(h, 0.0));
    for (size_t i = 1; i < n; i++) {
        for (int y = 0; y < h; y++) {
            const uint8_t* r1 = &gs[i - 1].px[(size_t)y * w];
            const uint8_t* r2 = &gs[i].px[(size_t)y * w];
            double s = 0;
            for (int x = 0; x < w; x++) s += std::abs((int)r1[x] - (int)r2[x]);
            diff[i - 1][y] = s / (double)w;
        }
    }

    const size_t P = n - 1;
    std::vector<double> col(P);
    // 第 90 百分位：容忍个别帧的异常（如状态栏时钟跳变）
    auto p90 = [&](int y) {
        for (size_t i = 0; i < P; i++) col[i] = diff[i][y];
        std::sort(col.begin(), col.end());
        const double rank = 0.9 * (double)(P - 1);
        const size_t lo = (size_t)std::floor(rank);
        const size_t hi = (size_t)std::ceil(rank);
        const double fr = rank - (double)lo;
        return col[lo] * (1.0 - fr) + col[hi] * fr;
    };

    int t = 0;
    while (t < h && p90(t) < 3.0) t++;
    int b = 0;
    while (b < h - 1 - t && p90(h - 1 - b) < 3.0) b++;
    if ((int64_t)t * 100 > (int64_t)max_ratio_pct * h) t = 0;
    if ((int64_t)b * 100 > (int64_t)max_ratio_pct * h) b = 0;
    *top_s = t;
    *bottom_s = b;
}

int32_t run_pipeline(LsPlan& plan, ls_get_frame_fn get_frame, void* user,
                     LsResult** out_result) {
    if (!out_result) return LS_ERR_INVALID_ARG;
    *out_result = nullptr;
    const int n = (int)plan.grays.size();
    if (n < 1) return LS_ERR_TOO_FEW_FRAMES;

    // ---- 1. 固定区域（状态栏/导航栏等） ----
    int top_s = 0, bottom_s = 0;
    const int max_ratio = plan.opt.max_chrome_ratio > 0 ? plan.opt.max_chrome_ratio : 15;
    if (n >= 2 && plan.opt.auto_detect_chrome)
        detect_static_rows(plan.grays, &top_s, &bottom_s, max_ratio);

    // 手动裁剪换算到灰度坐标，与自动检测一并作用于灰度图，
    // 保证对齐与合成处于同一坐标系
    const int manual_top_s    = (int)std::lround(plan.opt.crop_top / plan.yscale);
    const int manual_bottom_s = (int)std::lround(plan.opt.crop_bottom / plan.yscale);
    const int top_s_total    = top_s + manual_top_s;
    const int bottom_s_total = bottom_s + manual_bottom_s;

    // 全分辨率裁剪（手动部分保持精确像素值）
    int crop_t = plan.opt.crop_top + (int)std::lround(top_s * plan.yscale);
    int crop_b = plan.opt.crop_bottom + (int)std::lround(bottom_s * plan.yscale);
    if (crop_t < 0) crop_t = 0;
    if (crop_b < 0) crop_b = 0;
    if (crop_t + crop_b > plan.full_h - 16) { // 至少保留 16px 内容
        crop_t = 0;
        crop_b = 0;
    }
    const int content_h = plan.full_h - crop_t - crop_b;

    // ---- 2. 裁剪后的灰度图 ----
    const int hc = plan.scaled_h - top_s_total - bottom_s_total;
    if (hc < 4) return LS_ERR_INVALID_ARG;
    std::vector<GrayImage> cropped;
    cropped.reserve(n);
    for (const GrayImage& g : plan.grays) {
        GrayImage c;
        c.w = g.w;
        c.h = hc;
        c.px.insert(c.px.end(),
                    g.px.begin() + (size_t)top_s_total * g.w,
                    g.px.begin() + (size_t)(top_s_total + hc) * g.w);
        cropped.push_back(std::move(c));
    }

    // ---- 3. 逐帧决策（对齐基准始终是“上一个保留帧”，新增内容随帧累积） ----
    AlignParams ap;
    ap.band_count     = plan.opt.band_count;
    ap.min_accept_ncc = plan.opt.min_accept_ncc > 0 ? plan.opt.min_accept_ncc : 0.55f;
    ap.dedupe_ncc     = plan.opt.dedupe_ncc > 0 ? plan.opt.dedupe_ncc : 0.995f;
    // 无新内容阈值：新增 < max(1.5 行灰度, 0.35% 帧高) 视为页底
    const float no_new_rows = plan.opt.no_new_ratio > 0
                                  ? (1.f - plan.opt.no_new_ratio) * hc
                                  : std::max(1.5f, 0.0035f * hc);

    std::vector<Decision> dec(n);
    dec[0].kept = true;
    int last_kept = 0;
    bool last_no_new = false;
    int gap_count = 0, dropped = 0;

    for (int i = 1; i < n; i++) {
        const AlignResult ar = align_pair(cropped[last_kept], cropped[i], ap);
        dec[i].score = ar.score;
        if (!ar.confident) {
            dec[i].kept = true;
            dec[i].gap = true;
            gap_count++;
            last_kept = i;
            last_no_new = false;
        } else if ((double)hc - (double)ar.shift <= (double)no_new_rows) {
            dec[i].dropped = true; // 相对上一保留帧几乎没有新内容（已到页底）
            dropped++;
            last_no_new = true;
        } else {
            dec[i].kept = true;
            dec[i].overlap = ar.shift;
            last_kept = i;
            last_no_new = false;
        }
    }
    const bool reached_bottom = last_no_new;

    // ---- 4. 输出尺寸 ----
    std::vector<int> ov_full(n, 0);
    for (int i = 1; i < n; i++)
        if (dec[i].kept)
            ov_full[i] = std::clamp((int)std::lround(dec[i].overlap * plan.yscale),
                                    0, content_h - 1);

    int64_t out_h64 = (int64_t)crop_t + content_h;
    for (int i = 1; i < n; i++)
        if (dec[i].kept) out_h64 += content_h - ov_full[i];
    out_h64 += crop_b;
    if (out_h64 > INT32_MAX / 4) return LS_ERR_OUT_OF_MEMORY;
    const int out_h = (int)out_h64;

    // ---- 5. 合成 ----
    LsResult* r = (LsResult*)std::calloc(1, sizeof(LsResult));
    if (!r) return LS_ERR_OUT_OF_MEMORY;
    r->width = plan.full_w;
    r->height = out_h;
    r->frame_count = n;
    r->gap_count = gap_count;
    r->dropped_count = dropped;
    r->reached_bottom = reached_bottom ? 1 : 0;
    r->data = (uint8_t*)std::malloc((size_t)plan.full_w * out_h * 4);
    r->frames = (LsFrameInfo*)std::calloc((size_t)n, sizeof(LsFrameInfo));
    if (!r->data || !r->frames) {
        ls_result_free(r);
        return LS_ERR_OUT_OF_MEMORY;
    }

    LsImage frame{};
    if (get_frame(0, &frame, user) != LS_OK || !validate_frame(frame, plan)) {
        ls_result_free(r);
        return LS_ERR_STATE;
    }
    if (crop_t > 0) copy_frame_rows(frame, 0, crop_t, r->data, 0, plan.full_w);
    copy_frame_rows(frame, crop_t, crop_t + content_h, r->data, crop_t, plan.full_w);
    r->frames[0].source_index = 0;
    r->frames[0].offset_y = crop_t;
    r->frames[0].score = 1.0f;

    int cursor = crop_t + content_h;
    for (int i = 1; i < n; i++) {
        if (!dec[i].kept) continue;
        if (get_frame(i, &frame, user) != LS_OK || !validate_frame(frame, plan)) {
            ls_result_free(r);
            return LS_ERR_STATE;
        }
        const int dest_y = cursor - ov_full[i];
        copy_frame_rows(frame, crop_t, crop_t + content_h, r->data, dest_y, plan.full_w);
        cursor = dest_y + content_h;
        r->frames[i].source_index = i;
        r->frames[i].offset_y = dest_y;
        r->frames[i].score = dec[i].score;
        r->frames[i].gap = dec[i].gap ? 1 : 0;
    }
    for (int i = 0; i < n; i++) {
        if (dec[i].dropped) {
            r->frames[i].source_index = i;
            r->frames[i].offset_y = -1;
            r->frames[i].score = dec[i].score;
            r->frames[i].dropped = 1;
        }
    }

    if (crop_b > 0) {
        if (get_frame(last_kept, &frame, user) != LS_OK || !validate_frame(frame, plan)) {
            ls_result_free(r);
            return LS_ERR_STATE;
        }
        copy_frame_rows(frame, plan.full_h - crop_b, plan.full_h,
                        r->data, cursor, plan.full_w);
    }

    // 强制不透明（部分截屏管线 alpha 未定义）
    for (int64_t i = 3; i < (int64_t)plan.full_w * out_h * 4; i += 4)
        r->data[i] = 255;

    *out_result = r;
    return LS_OK;
}

} // namespace ls
