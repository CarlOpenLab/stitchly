// Longshot core 单元测试：合成"长页面 + 模拟滚动截帧"夹具
//
// 夹具原理：程序化生成一张高对比纹理长页（卡片 + 块状噪点模拟文字），
// 按设定的滚动步长从页面截取带状态栏/导航栏的帧，可选加噪 / 动态区域，
// 然后调用 core 拼接，断言高度、去重、页底检测与内容对齐。
#include "longshot/stitch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------- 确定性哈希纹理 ----------------
static inline uint32_t hash2(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE3Du;
    h ^= h >> 16;
    return h;
}

// ---------------- 合成长页面 ----------------
struct Page {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba; // w*h*4
    const uint8_t* row(int y) const { return &rgba[(size_t)y * w * 4]; }
};

static Page make_page(int w, int h, uint32_t seed) {
    Page p;
    p.w = w;
    p.h = h;
    p.rgba.assign((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++) {
        // 每页独有的"地标"行：制造页面间结构性差异，避免不同页面
        // 因同构布局产生周期性互相关（NCC 对亮度仿射变换不敏感）
        const bool landmark = (hash2(seed, (uint32_t)y / 6) % 100) < 4;
        for (int x = 0; x < w; x++) {
            uint8_t v;
            if (landmark) {
                v = (uint8_t)(50 + hash2((uint32_t)x + seed * 7u, (uint32_t)y) % 80);
            } else if ((hash2(x / 2 + seed, y / 2) % 100) < 50) {
                v = (uint8_t)(40 + hash2((uint32_t)x + seed, (uint32_t)y) % 60);
            } else {
                v = 235;
            }
            uint8_t* px = &p.rgba[((size_t)y * w + x) * 4];
            px[0] = v; px[1] = v; px[2] = v; px[3] = 255;
        }
    }
    return p;
}

// ---------------- 帧捕获模拟 ----------------
static const int kDynY0 = 2000, kDynH = 400; // 动态区域（页面坐标）

struct FrameSpec {
    int scroll    = 0;
    int noise_amp = 0;
    bool dynamic  = false;
};

static std::vector<uint8_t> capture_frame(const Page& page, int fw, int fh,
                                          int chrome_top, int chrome_bottom,
                                          const FrameSpec& spec, int frame_idx) {
    std::vector<uint8_t> out((size_t)fw * fh * 4, 0);
    for (int y = 0; y < fh; y++) {
        for (int x = 0; x < fw; x++) {
            uint8_t v;
            if (y < chrome_top) {
                v = 208; // 状态栏
            } else if (y >= fh - chrome_bottom) {
                v = 120; // 底栏
            } else {
                const int py = spec.scroll + (y - chrome_top);
                if (spec.dynamic && py >= kDynY0 && py < kDynY0 + kDynH &&
                    x >= fw * 3 / 10 && x < fw * 7 / 10) {
                    v = (uint8_t)(hash2(x, py * 97 + (uint32_t)frame_idx * 131) % 256);
                } else if (py >= 0 && py < page.h) {
                    v = page.row(py)[x * 4];
                } else {
                    v = 235;
                }
            }
            if (spec.noise_amp > 0) {
                const int n = (int)(hash2((uint32_t)x + (uint32_t)frame_idx * 7919u,
                                          (uint32_t)y * 31u + (uint32_t)frame_idx) %
                                    (2 * spec.noise_amp + 1)) - spec.noise_amp;
                v = (uint8_t)std::clamp((int)v + n, 0, 255);
            }
            uint8_t* px = &out[((size_t)y * fw + x) * 4];
            px[0] = v; px[1] = v; px[2] = v; px[3] = 255;
        }
    }
    return out;
}

// ---------------- 断言与检查 ----------------
static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  [FAIL] line %d: %s\n", __LINE__, msg);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

// 内容对齐抽查：输出内容行应对应页面行 (y_out - content_top)（允许累计漂移）
static void check_content_alignment(const LsResult* res, const Page& page,
                                    int content_top) {
    const int search = 60; // 搜索窗口
    const int tol = 40;    // 允许的累计漂移
    const int samples[] = {res->height / 6, res->height / 3, res->height / 2,
                           2 * res->height / 3, 5 * res->height / 6};
    const int strip = 40;
    for (int y_out : samples) {
        if (y_out < content_top + strip || y_out + strip > res->height) continue;
        int best_diff = 1 << 30, best_py = -1000000000;
        for (int py = y_out - content_top - search; py <= y_out - content_top + search; py++) {
            if (py < 0 || py + strip > page.h) continue;
            long d = 0;
            for (int dy = 0; dy < strip; dy++) {
                const uint8_t* ro = res->data + (size_t)(y_out + dy) * res->width * 4;
                const uint8_t* rp = page.row(py + dy);
                for (int x = 0; x < res->width; x += 4)
                    d += std::abs((int)ro[x * 4] - (int)rp[x * 4]);
            }
            if (d < best_diff) { best_diff = (int)d; best_py = py; }
        }
        const int expected = y_out - content_top;
        CHECK(std::abs(best_py - expected) <= tol, "content row drift within tolerance");
        CHECK(best_diff / (strip * (res->width / 4)) < 15, "content matches page texture");
    }
}

struct Frames {
    std::vector<std::vector<uint8_t>> buf;
    std::vector<LsImage> imgs;
    int w = 0, h = 0;

    void build(const Page& page, int fw, int fh, int ct, int cb,
               const std::vector<FrameSpec>& specs) {
        w = fw; h = fh;
        buf.clear(); imgs.clear();
        for (size_t i = 0; i < specs.size(); i++) {
            buf.push_back(capture_frame(page, fw, fh, ct, cb, specs[i], (int)i));
            imgs.push_back(LsImage{buf.back().data(), fw, fh, 0});
        }
    }
};

// ---------------- 用例 ----------------

static void test_basic() {
    std::printf("test_basic\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 1);
    Frames f;
    f.build(page, W, FH, CT, CB, {{0}, {760}, {1520}, {2280}, {3040}, {3800}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->height >= 6170 && res->height <= 6230, "height within tolerance");
    CHECK(res->dropped_count == 0, "no dropped frames");
    CHECK(res->gap_count == 0, "no gaps");
    CHECK(res->reached_bottom == 0, "not bottom");
    check_content_alignment(res, page, CT);
    ls_result_free(res);
}

static void test_auto_chrome() {
    std::printf("test_auto_chrome\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 2);
    Frames f;
    f.build(page, W, FH, CT, CB, {{0}, {760}, {1520}, {2280}, {3040}, {3800}});

    LsOptions opt;
    ls_options_init(&opt); // auto_detect_chrome = 1, 手动裁剪 0

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    // 状态栏/导航栏应被检测并各保留一份；高度含固定区
    CHECK(res->height >= 6150 && res->height <= 6250, "height incl. chrome within tolerance");
    CHECK(res->dropped_count == 0, "no dropped frames");
    CHECK(res->gap_count == 0, "no gaps");
    ls_result_free(res);
}

static void test_dynamic() {
    std::printf("test_dynamic\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 3);
    Frames f;
    f.build(page, W, FH, CT, CB,
            {{0, 0, true}, {760, 0, true}, {1520, 0, true},
             {2280, 0, true}, {3040, 0, true}, {3800, 0, true}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    // 动态区域占 40% 宽，分带投票应取到干净的带
    CHECK(res->gap_count == 0, "no gaps despite dynamic block");
    CHECK(res->height >= 6160 && res->height <= 6240, "height within tolerance");
    ls_result_free(res);
}

static void test_duplicates() {
    std::printf("test_duplicates\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 4);
    Frames f;
    f.build(page, W, FH, CT, CB, {{0}, {700}, {700}, {700}, {1400}, {2100}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->dropped_count == 2, "2 duplicate frames dropped");
    CHECK(res->height >= 4470 && res->height <= 4530, "height within tolerance");
    CHECK(res->reached_bottom == 0, "not bottom");
    ls_result_free(res);
}

static void test_bottom() {
    std::printf("test_bottom\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 5);
    Frames f;
    // 最后两帧停在页底：最后一帧相对上一保留帧无新内容
    f.build(page, W, FH, CT, CB, {{0}, {900}, {1800}, {2700}, {3600}, {3800}, {3800}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->reached_bottom == 1, "reached bottom detected");
    CHECK(res->dropped_count >= 1, "tail frame dropped");
    CHECK(res->height >= 6170 && res->height <= 6230, "height within tolerance");
    ls_result_free(res);
}

static void test_slow_scroll() {
    std::printf("test_slow_scroll\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 6);
    std::vector<FrameSpec> specs;
    for (int i = 0; i < 26; i++) specs.push_back({i * 60, 0, false}); // 慢速匀速滚动
    Frames f;
    f.build(page, W, FH, CT, CB, specs);

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->dropped_count == 0, "slow scroll frames all kept");
    CHECK(res->height >= 3870 && res->height <= 3930, "height within tolerance");
    check_content_alignment(res, page, CT);
    ls_result_free(res);
}

static void test_noise() {
    std::printf("test_noise\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 7);
    Frames f;
    f.build(page, W, FH, CT, CB,
            {{0, 8}, {760, 8}, {1520, 8}, {2280, 8}, {3040, 8}, {3800, 8}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->gap_count == 0, "no gaps under noise");
    CHECK(res->height >= 6150 && res->height <= 6250, "height within tolerance");
    ls_result_free(res);
}

static void test_two_frames() {
    std::printf("test_two_frames\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 8);
    Frames f;
    f.build(page, W, FH, CT, CB, {{0}, {1400}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->height >= 3770 && res->height <= 3830, "height within tolerance");
    ls_result_free(res);
}

static void test_gap() {
    std::printf("test_gap\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 3000;
    Page p1 = make_page(W, PAGE_H, 11);
    Page p2 = make_page(W, PAGE_H, 999); // 不相关页面
    Frames f;
    f.buf.push_back(capture_frame(p1, W, FH, CT, CB, {0}, 0));
    f.buf.push_back(capture_frame(p2, W, FH, CT, CB, {0}, 1));
    f.imgs.push_back(LsImage{f.buf[0].data(), W, FH, 0});
    f.imgs.push_back(LsImage{f.buf[1].data(), W, FH, 0});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    LsResult* res = nullptr;
    const int rc = ls_stitch_images(f.imgs.data(), 2, &opt, &res);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) return;
    CHECK(res->gap_count == 1, "uncorrelated frames reported as gap");
    CHECK(res->height == CT + 2 * (FH - CT - CB) + CB, "gap appends full frame");
    ls_result_free(res);
}

static int32_t stream_get_frame(int32_t index, LsImage* out, void* user) {
    auto* frames = (Frames*)user;
    if (index < 0 || index >= (int)frames->buf.size()) return -1;
    out->data = frames->buf[index].data();
    out->width = frames->w;
    out->height = frames->h;
    out->stride = 0;
    return LS_OK;
}

static void test_streaming() {
    std::printf("test_streaming\n");
    const int W = 1080, FH = 2400, CT = 90, CB = 110, PAGE_H = 6000;
    Page page = make_page(W, PAGE_H, 12);
    Frames f;
    f.build(page, W, FH, CT, CB, {{0}, {760}, {1520}, {2280}, {3040}, {3800}});

    LsOptions opt;
    ls_options_init(&opt);
    opt.auto_detect_chrome = 0;
    opt.crop_top = CT;
    opt.crop_bottom = CB;

    // 流式 API
    LsPlan* plan = ls_plan_create(W, FH, &opt);
    CHECK(plan != nullptr, "plan created");
    if (!plan) return;
    for (const LsImage& im : f.imgs)
        CHECK(ls_plan_add_frame(plan, &im) == LS_OK, "add frame");
    CHECK(ls_plan_frame_count(plan) == 6, "frame count");
    CHECK(ls_plan_last_frame_duplicate(plan) == 0, "last frames differ");

    LsResult* res2 = nullptr;
    const int rc = ls_plan_compose(plan, stream_get_frame, &f, &res2);
    CHECK(rc == LS_OK, ls_error_string(rc));
    if (rc != LS_OK) { ls_plan_free(plan); return; }

    // 与一次性 API 结果一致性
    LsResult* res1 = nullptr;
    CHECK(ls_stitch_images(f.imgs.data(), (int)f.imgs.size(), &opt, &res1) == LS_OK,
          "one-shot stitch");
    CHECK(res1 && res2->height == res1->height, "streaming == one-shot height");
    if (res1 && res2->height == res1->height) {
        const size_t bytes = (size_t)res1->width * res1->height * 4;
        CHECK(std::memcmp(res1->data, res2->data, bytes) == 0, "pixels identical");
    }
    ls_result_free(res1);
    ls_result_free(res2);

    // 实时重复帧查询：连续相同帧
    Frames g;
    g.build(page, W, FH, CT, CB, {{0}, {0}});
    LsPlan* p2 = ls_plan_create(W, FH, &opt);
    for (const LsImage& im : g.imgs) ls_plan_add_frame(p2, &im);
    CHECK(ls_plan_last_frame_duplicate(p2) == 1, "identical frames detected as duplicate");
    ls_plan_free(p2);

    ls_plan_free(plan);
}

static void test_errors() {
    std::printf("test_errors\n");
    LsResult* res = nullptr;
    LsOptions opt;
    ls_options_init(&opt);

    CHECK(ls_stitch_images(nullptr, 2, &opt, &res) == LS_ERR_INVALID_ARG, "null images");
    CHECK(ls_stitch_images(nullptr, 0, &opt, &res) == LS_ERR_INVALID_ARG, "null images 0");
    LsImage dummy{nullptr, 100, 100, 0};
    CHECK(ls_stitch_images(&dummy, 0, &opt, &res) == LS_ERR_TOO_FEW_FRAMES, "zero frames");
    CHECK(ls_stitch_images(&dummy, 1, &opt, &res) == LS_ERR_INVALID_ARG, "null data");

    uint8_t px[64 * 64 * 4] = {0};
    LsImage small{px, 64, 64, 0};
    CHECK(ls_stitch_images(&small, 1, &opt, &res) == LS_OK, "single frame ok");
    if (res) {
        CHECK(res->height == 64 && res->width == 64, "single frame passthrough");
        ls_result_free(res);
        res = nullptr;
    }

    // 帧尺寸不一致
    uint8_t px2[32 * 64 * 4] = {0};
    LsImage pair[2] = {{px, 64, 64, 0}, {px2, 32, 64, 0}};
    CHECK(ls_stitch_images(pair, 2, &opt, &res) == LS_ERR_INVALID_ARG, "mixed sizes");

    // 流式 API 参数校验
    CHECK(ls_plan_create(0, 100, &opt) == nullptr, "bad plan dims");
    LsPlan* plan = ls_plan_create(64, 64, &opt);
    CHECK(plan != nullptr, "plan created");
    LsImage wrong{px2, 32, 64, 0};
    CHECK(ls_plan_add_frame(plan, &wrong) == LS_ERR_INVALID_ARG, "wrong size frame");
    CHECK(ls_plan_compose(plan, nullptr, nullptr, &res) == LS_ERR_INVALID_ARG, "null callback");
    ls_plan_free(plan);
}

int main() {
    test_basic();
    test_auto_chrome();
    test_dynamic();
    test_duplicates();
    test_bottom();
    test_slow_scroll();
    test_noise();
    test_two_frames();
    test_gap();
    test_streaming();
    test_errors();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
