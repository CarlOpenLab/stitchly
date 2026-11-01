/*
 * Longshot - 长截图拼接核心（C API）
 *
 * 纯 C++17 实现，无第三方依赖。Android 通过 JNI、iOS 通过 Objective-C++
 * 桥接调用本头文件。
 *
 * 设计约定：
 *  - 输入为同一设备、同宽度的连续屏幕截图（RGBA8888，行优先）。
 *  - 对齐在降采样灰度图上进行（默认宽 200px），合成使用全分辨率原图。
 *  - 顶部/底部固定区域（状态栏、导航栏、键盘等）可手动指定裁剪，亦可
 *    自动检测（跨帧恒定的行），合成时各保留一份于输出首尾。
 */
#ifndef LONGSHOT_STITCH_H
#define LONGSHOT_STITCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 错误码 ---------------- */
#define LS_OK                   0
#define LS_ERR_INVALID_ARG    (-1)  /* 参数非法（空指针 / 尺寸过小 / 帧尺寸不一致） */
#define LS_ERR_TOO_FEW_FRAMES (-2)  /* 帧数不足 */
#define LS_ERR_OUT_OF_MEMORY  (-3)
#define LS_ERR_STATE          (-4)  /* 状态错误（如回调取帧失败） */

/* ---------------- 数据结构 ---------------- */

/* 一帧 RGBA8888 图像 */
typedef struct LsImage {
    const uint8_t* data;   /* 像素数据，行优先，每像素 4 字节（R,G,B,A） */
    int32_t        width;
    int32_t        height;
    int32_t        stride; /* 每行字节数；0 表示 width*4 */
} LsImage;

/* 拼接选项；零值字段会取默认值，也可先用 ls_options_init 填默认值 */
typedef struct LsOptions {
    int32_t crop_top;           /* 手动指定每帧顶部固定区域像素数（全分辨率） */
    int32_t crop_bottom;        /* 手动指定每帧底部固定区域像素数（全分辨率） */
    int32_t align_width;        /* 对齐用降采样宽度，0 => 200 */
    int32_t band_count;         /* 垂直分带投票数（抗动态内容），0 => 4 */
    float   dedupe_ncc;         /* 判定重复帧的 NCC 阈值，0 => 0.995f */
    float   min_accept_ncc;     /* 低于此值视为无法对齐（gap），0 => 0.55f */
    float   no_new_ratio;       /* 新增内容少于帧高的该比例 => 无新内容（页底），
                                   0 => 自动（约 1.5 行灰度图，~8 物理像素） */
    int32_t auto_detect_chrome; /* 1 => 自动检测跨帧恒定的顶部/底部行 */
    int32_t max_chrome_ratio;   /* 自动检测的固定区占比上限（百分比），0 => 15 */
} LsOptions;

/* 单帧诊断信息 */
typedef struct LsFrameInfo {
    int32_t source_index; /* 对应输入帧下标 */
    int32_t offset_y;     /* 该帧裁剪后内容在输出图中的起始 y；丢弃帧为 -1 */
    float   score;        /* 与上一保留帧对齐的 NCC 得分；首帧为 1.0 */
    int32_t dropped;      /* 1 => 重复/无新内容，未参与合成 */
    int32_t gap;          /* 1 => 未能置信对齐，整帧追加（可能缺内容） */
} LsFrameInfo;

/* 拼接结果，用 ls_result_free 释放 */
typedef struct LsResult {
    uint8_t*     data;          /* RGBA8888 拼接结果（stride = width*4） */
    int32_t      width;
    int32_t      height;
    LsFrameInfo* frames;        /* 与输入帧一一对应 */
    int32_t      frame_count;
    int32_t      reached_bottom;/* 1 => 最后一帧相对此前无新内容（已到页底） */
    int32_t      dropped_count; /* 重复/无新内容被丢弃的帧数 */
    int32_t      gap_count;     /* 未能置信对齐、整帧追加的次数 */
} LsResult;

/* ---------------- 一次性 API（相册模式） ----------------
 * 所有帧常驻内存时使用。要求 images[0..count-1] 尺寸一致。
 * count < 1 返回 LS_ERR_TOO_FEW_FRAMES。 */
int32_t ls_stitch_images(const LsImage* images, int32_t count,
                         const LsOptions* options, LsResult** out_result);

/* ---------------- 流式 API（录屏/实时抓帧模式） ----------------
 * add_frame 内部仅保留降采样灰度图，调用后可立即释放全分辨率帧；
 * compose 阶段通过 get_frame 回调按需逐帧取回全分辨率数据。 */

typedef struct LsPlan LsPlan;

/* 取帧回调：填充 out 并返回 LS_OK；data 仅需在本次调用内有效。
 * index 为输入帧下标 [0, 帧总数)。返回非 0 视为取帧失败。 */
typedef int32_t (*ls_get_frame_fn)(int32_t index, LsImage* out, void* user);

LsPlan* ls_plan_create(int32_t frame_width, int32_t frame_height,
                       const LsOptions* options);
/* 帧尺寸必须与 create 时一致；返回 LS_OK 或错误码 */
int32_t ls_plan_add_frame(LsPlan* plan, const LsImage* frame);
int32_t ls_plan_frame_count(const LsPlan* plan);
/* 最后一帧是否与前一帧几乎相同（用于滚动停止的快速判定） */
int32_t ls_plan_last_frame_duplicate(const LsPlan* plan);
/* 计算对齐并合成。get_frame 会被按需调用（每帧至多两次）。 */
int32_t ls_plan_compose(LsPlan* plan, ls_get_frame_fn get_frame, void* user,
                        LsResult** out_result);
void    ls_plan_free(LsPlan* plan);

/* ---------------- 工具 ---------------- */
void        ls_options_init(LsOptions* options);
const char* ls_error_string(int32_t code);
void        ls_result_free(LsResult* result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LONGSHOT_STITCH_H */
