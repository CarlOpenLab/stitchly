// Longshot core 内部共享定义（不对外发布）
#pragma once

#include "longshot/stitch.h"

#include <cstdint>
#include <vector>

namespace ls {

// 灰度平均差低于该值视为与前一帧相同（重复帧快路径）
inline constexpr double kDupDiffThreshold = 2.0;

// 降采样灰度图（对齐用）
struct GrayImage {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px; // w*h，行优先
};

// RGBA -> 灰度降采样（盒滤波）
GrayImage downscale_rgba_to_gray(const LsImage& img, int target_w);

// 两幅同尺寸灰度图的平均绝对差（0~255）
double mean_abs_diff(const GrayImage& a, const GrayImage& b);

struct AlignParams {
    int   band_count     = 4;
    float min_accept_ncc = 0.55f;
    float dedupe_ncc     = 0.995f;
};

struct AlignResult {
    float shift     = 0.f;  // b 顶部与 a 底部重叠的行数（灰度图坐标，可为小数）
    float score     = 0.f;  // 各带 NCC 的均值
    bool  confident = false;
};

// 垂直对齐：在 shift ∈ [min_overlap, min(ha,hb)] 内搜索各竖带 NCC 最优值，
// 取带间中位数投票，亚像素抛物线精化。
AlignResult align_pair(const GrayImage& a, const GrayImage& b, const AlignParams& p);

// 检测跨帧恒定的顶部/底部行数（灰度图坐标），用于状态栏/导航栏裁剪。
// 逐行取各相邻帧对差异的第 90 百分位，连续 <3 的行视为固定区域；
// 超过 max_ratio_pct 帧高的连续区不视为固定区（防误裁）。
void detect_static_rows(const std::vector<GrayImage>& gs,
                        int* top_s, int* bottom_s,
                        int max_ratio_pct);

} // namespace ls

// ---------------- 流式拼接计划 ----------------
// 全局作用域定义，与 stitch.h 中 opaque 的 `typedef struct LsPlan LsPlan` 同一实体
struct LsPlan {
    LsOptions opt{};
    int   full_w = 0, full_h = 0;
    int   align_w = 0;
    int   scaled_h = 0;
    float yscale = 1.f;                    // full_h / scaled_h
    std::vector<ls::GrayImage> grays;      // 各帧降采样灰度图（未裁剪）
    std::vector<double> seq_diff;          // frame[i] 与 frame[i-1] 的平均绝对差
};

namespace ls {
// 主流水线：对齐决策 + 合成。stitcher.cpp 实现。
int32_t run_pipeline(LsPlan& plan, ls_get_frame_fn get_frame, void* user,
                     LsResult** out_result);
} // namespace ls
