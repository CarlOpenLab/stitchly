// 垂直对齐：分带 NCC 搜索 + 中位数投票 + 亚像素精化
#include "internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ls {
namespace {

struct BandTables {
    int bw = 0, x0 = 0, x1 = 0;
    // 每行灰度和 / 平方和（本带列范围）
    std::vector<double> a_sum, a_sq, b_sum, b_sq;
    // 前缀和（长度 ha+1 / hb+1，pref[i] = sum of rows [0, i)）
    std::vector<double> a_sum_pre, a_sq_pre, b_sum_pre, b_sq_pre;
    // 交叉项：对角线 d = y1 - y2 ∈ [dlo, dhi]，cross_pre[di][k] = Σ_{t<k} dot(a[y2+d], b[y2]) at y2=t
    // 即对 shift s（d = ha - s），S12 = cross_pre[di][s-1 + 1] 的前 s 项和
    std::vector<std::vector<double>> cross_pre; // [d - dlo][k+1] = Σ_{t<=k} ...
};

void build_band(const GrayImage& a, const GrayImage& b,
                int x0, int x1, int dlo, int dhi, BandTables& t) {
    const int ha = a.h, hb = b.h;
    t.x0 = x0; t.x1 = x1; t.bw = x1 - x0;

    t.a_sum.assign(ha, 0.0); t.a_sq.assign(ha, 0.0);
    t.b_sum.assign(hb, 0.0); t.b_sq.assign(hb, 0.0);
    for (int y = 0; y < ha; y++) {
        const uint8_t* r = &a.px[(size_t)y * a.w];
        double s = 0, q = 0;
        for (int x = x0; x < x1; x++) { double v = r[x]; s += v; q += v * v; }
        t.a_sum[y] = s; t.a_sq[y] = q;
    }
    for (int y = 0; y < hb; y++) {
        const uint8_t* r = &b.px[(size_t)y * b.w];
        double s = 0, q = 0;
        for (int x = x0; x < x1; x++) { double v = r[x]; s += v; q += v * v; }
        t.b_sum[y] = s; t.b_sq[y] = q;
    }
    t.a_sum_pre.assign(ha + 1, 0.0); t.a_sq_pre.assign(ha + 1, 0.0);
    for (int y = 0; y < ha; y++) {
        t.a_sum_pre[y + 1] = t.a_sum_pre[y] + t.a_sum[y];
        t.a_sq_pre[y + 1]  = t.a_sq_pre[y] + t.a_sq[y];
    }
    t.b_sum_pre.assign(hb + 1, 0.0); t.b_sq_pre.assign(hb + 1, 0.0);
    for (int y = 0; y < hb; y++) {
        t.b_sum_pre[y + 1] = t.b_sum_pre[y] + t.b_sum[y];
        t.b_sq_pre[y + 1]  = t.b_sq_pre[y] + t.b_sq[y];
    }

    // 交叉项（只需 d ∈ [dlo, dhi] 的对角线）
    const int nd = dhi - dlo + 1;
    t.cross_pre.assign(nd, {});
    for (int di = 0; di < nd; di++) {
        const int d = dlo + di;
        const int valid = std::min(hb, ha - d); // y2 范围 [0, valid)
        std::vector<double>& pre = t.cross_pre[di];
        pre.assign(hb + 1, 0.0);
        double acc = 0;
        for (int y2 = 0; y2 < valid; y2++) {
            const uint8_t* ra = &a.px[(size_t)(y2 + d) * a.w];
            const uint8_t* rb = &b.px[(size_t)y2 * b.w];
            double s = 0;
            for (int x = x0; x < x1; x++) s += (double)ra[x] * rb[x];
            acc += s;
            pre[y2 + 1] = acc;
        }
        // y2 >= valid 时贡献为 0，前缀保持不变
    }
}

// shift s 处、单带 NCC
double band_ncc(const BandTables& t, int ha, int hb, int s) {
    const int d = ha - s;
    const int di = d - (ha - std::min(ha, hb)); // dlo = ha - smax
    if (di < 0 || di >= (int)t.cross_pre.size()) return 0.0;
    const double N = (double)s * t.bw;
    if (N < 1.0) return 0.0;

    const double S1  = t.a_sum_pre[ha] - t.a_sum_pre[ha - s];
    const double S2  = t.a_sq_pre[ha] - t.a_sq_pre[ha - s];
    const double S3  = t.b_sum_pre[s];
    const double S4  = t.b_sq_pre[s];
    const double S12 = t.cross_pre[di][s];

    const double var1 = S2 - S1 * S1 / N;
    const double var2 = S4 - S3 * S3 / N;
    // 退化（纯色区域）判定：相对方差极小
    if (var1 < 1e-4 * (std::abs(S2) + 1e-9) || var2 < 1e-4 * (std::abs(S4) + 1e-9))
        return 0.0;
    const double cov = S12 - S1 * S3 / N;
    double ncc = cov / std::sqrt(var1 * var2);
    return std::max(-1.0, std::min(1.0, ncc));
}

} // namespace

AlignResult align_pair(const GrayImage& a, const GrayImage& b, const AlignParams& p) {
    AlignResult res;
    if (a.w != b.w || a.px.empty() || b.px.empty()) return res;
    const int ha = a.h, hb = b.h, w = a.w;

    int bands = p.band_count > 0 ? p.band_count : 4;
    if (bands > 8) bands = 8;
    while (bands > 1 && w / bands < 8) bands--;

    const int smax   = std::min(ha, hb);
    const int min_ov = std::max(6, smax / 50);
    if (smax <= min_ov) return res; // 不可对齐：confident=false

    const int dlo = ha - smax;
    const int dhi = ha - min_ov;

    std::vector<BandTables> tables(bands);
    const int bw = w / bands;
    for (int i = 0; i < bands; i++) {
        const int x0 = i * bw;
        const int x1 = (i == bands - 1) ? w : x0 + bw;
        build_band(a, b, x0, x1, dlo, dhi, tables[i]);
    }

    // 各带最优 shift
    std::vector<int> best_s(bands, min_ov);
    for (int i = 0; i < bands; i++) {
        double best_v = -2.0;
        for (int s = min_ov; s <= smax; s++) {
            const double v = band_ncc(tables[i], ha, hb, s);
            if (v > best_v) { best_v = v; best_s[i] = s; }
        }
    }

    // 中位数投票（下中位数：动态带偏多时仍偏向干净带的值）
    std::vector<int> sorted(best_s);
    std::sort(sorted.begin(), sorted.end());
    int s0 = sorted[(bands - 1) / 2];
    if (s0 < min_ov) s0 = min_ov;
    if (s0 > smax) s0 = smax;

    // 各带在 s0 处 NCC 均值
    auto mean_ncc = [&](int s) {
        double acc = 0;
        for (int i = 0; i < bands; i++) acc += band_ncc(tables[i], ha, hb, s);
        return acc / bands;
    };

    double score = mean_ncc(s0);
    // 亚像素精化（抛物线），仅在内部点且曲线有极值形态时
    if (s0 > min_ov && s0 < smax) {
        const double n_lo = mean_ncc(s0 - 1);
        const double n_hi = mean_ncc(s0 + 1);
        const double denom = n_lo - 2.0 * score + n_hi;
        if (denom > 1e-9) {
            const double delta = 0.5 * (n_lo - n_hi) / denom;
            if (std::abs(delta) <= 1.0) {
                res.shift = (float)(s0 + delta);
            } else {
                res.shift = (float)s0;
            }
        } else {
            res.shift = (float)s0;
        }
    } else {
        res.shift = (float)s0;
    }

    res.score = (float)score;
    res.confident = score >= (double)p.min_accept_ncc;
    return res;
}

} // namespace ls
