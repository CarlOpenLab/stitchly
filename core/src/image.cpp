// 图像基础操作：降采样、灰度化、差异度量
#include "internal.h"

#include <algorithm>
#include <cmath>

namespace ls {

GrayImage downscale_rgba_to_gray(const LsImage& img, int target_w) {
    GrayImage out;
    const int stride = img.stride > 0 ? img.stride : img.width * 4;
    out.w = std::max(1, target_w);
    out.h = std::max(1, (int)std::lround((double)img.height * out.w / img.width));
    out.px.assign((size_t)out.w * out.h, 0);

    for (int y = 0; y < out.h; y++) {
        int y0 = (int)((int64_t)y * img.height / out.h);
        int y1 = (int)((int64_t)(y + 1) * img.height / out.h);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > img.height) y1 = img.height;
        uint8_t* dst = &out.px[(size_t)y * out.w];
        for (int x = 0; x < out.w; x++) {
            int x0 = (int)((int64_t)x * img.width / out.w);
            int x1 = (int)((int64_t)(x + 1) * img.width / out.w);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > img.width) x1 = img.width;
            double r = 0, g = 0, b = 0;
            int n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint8_t* row = img.data + (size_t)yy * stride;
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t* p = row + (size_t)xx * 4;
                    r += p[0]; g += p[1]; b += p[2];
                    n++;
                }
            }
            dst[x] = (uint8_t)std::lround((r * 0.299 + g * 0.587 + b * 0.114) / n);
        }
    }
    return out;
}

double mean_abs_diff(const GrayImage& a, const GrayImage& b) {
    if (a.w != b.w || a.h != b.h || a.px.empty() || b.px.empty()) return 255.0;
    double acc = 0;
    const size_t n = a.px.size();
    for (size_t i = 0; i < n; i++) acc += std::abs((int)a.px[i] - (int)b.px[i]);
    return acc / (double)n;
}

} // namespace ls
