# 架构与算法

## 总体架构

```
┌─────────────────────────── core (C++17, 无依赖) ───────────────────────────┐
│  C API: ls_stitch_images(一次性) / ls_plan_*(流式)                          │
│  ├─ 预处理: RGBA→降采样灰度(宽200)                                          │
│  ├─ 固定区检测: 跨帧恒定行(90分位) → 状态栏/导航栏裁剪                        │
│  ├─ 对齐: 分带 NCC 搜索 → 中位数投票 → 亚像素抛物线                          │
│  ├─ 决策: 无新内容(页底) / 重复帧丢弃 / gap(未置信对齐)                      │
│  └─ 合成: 全分辨率流式追加，固定区仅保留首/尾一份                            │
└──────────┬JNI(NDK静态链)─────────────────────┬ObjC++(源码编入target)───────┘
           │                                    │
   Android (Kotlin/Compose)              iOS (SwiftUI)
   ├─ CaptureService                     ├─ BroadcastExtension (ReplayKit)
   │   MediaProjection+ImageReader       │   CMSampleBuffer→AVAssetWriter
   │   ~8fps 抓帧→JPEG落盘+计划入帧       │   →H.264 流式写 App Group
   │   重复帧检测→自动停止                ├─ 主 App 轮询新视频
   ├─ AutoScrollService(可选)            │   AVAssetImageGenerator 抽帧
   │   无障碍手势模拟匀速上滑             │   →JPEG 落盘→计划→按文件合成
   ├─ FrameComposer(流式合成)            ├─ AlbumStitchView (PHPicker)
   └─ AlbumStitcher(一次性拼接)          └─ ResultView (保存/分享)
```

## 关键设计

### 1. 两种 C API 形态

- **一次性** `ls_stitch_images`：相册模式，帧全在内存（≤15 张）。
- **流式** `ls_plan_*`：录屏模式。`ls_plan_add_frame` 内部立即降采样，只保留
  ~90KB 的灰度图，调用方可立刻释放全分辨率帧；合成阶段经 `get_frame` 回调
  按需重新取帧（Android 从磁盘 JPEG 解码、iOS 从 JPEG 文件解码）。
  这使采集过程的常驻内存与帧数无关。

### 2. 对齐算法（align.cpp）

同设备截图同宽、无旋转缩放，只需解一维垂直偏移：

1. 每帧降采样到宽 200 的灰度图（盒滤波），裁掉固定区后高约 400 行。
2. 搜索 `shift ∈ [min_ov, min(ha,hb)]`：b 的顶部与 a 的底部重叠 `shift` 行。
   NCC（归一化互相关）用积分图加速；交叉项只需对角线 `d = ha - shift`，
   复杂度 O(h²·w/2) ≈ 20M 乘加/对，手机上约 30ms。
3. **分带投票**：宽度分 4 带（默认）各自求最优 shift，取**下中位数**，
   抵抗 GIF/视频轮播等动态区域（动态带的结果被干净带淹没）。
4. **亚像素**：对带均值 NCC 曲线在峰值处抛物线插值，典型精度 ±0.2 灰度行
   ≈ ±1 全分辨率像素，接缝几乎不可见。
5. NCC 分母退化（纯色区）时该带记 0 分；整体得分 < 0.55 判 gap。

### 3. 决策规则（stitcher.cpp）

- 对齐基准**始终是"上一个保留帧"**，新增内容随帧自然累积——慢速滚动时
  每帧新增几像素也不会丢内容（前几帧被丢弃的内容由后续保留帧补齐）。
- **无新内容（页底）**：新增 < max(1.5 灰度行, 0.35% 帧高) ≈ 8 物理像素。
  绝对行数阈值而非比例，避免慢速滚动帧被误判（曾用 3% 比例导致
  8fps 慢滚时整段内容丢失的 bug，见 git 历史）。
- **gap**：得分不足时整帧追加（宁重复不缺行），并在结果里计数供 UI 提示。
- **固定区**：逐行取相邻帧对差异的 90 百分位（容忍状态栏时钟跳变这类
  偶发变化），连续 <3 的行视为固定区，占比 >15% 则放弃该侧检测（防误裁
  页面本身静止的情况）。合成时顶部固定区取首帧、底部取最后一个保留帧。

### 4. 坐标系一致性（重要教训）

对齐在"灰度图坐标"上进行，合成在"全分辨率坐标"上进行。手动裁剪
（crop_top/crop_bottom）既要在全分辨率合成时生效，也**必须换算到灰度图
坐标一并裁掉灰度图**，否则对齐得到的 shift 含固定区高度、合成却按内容
坐标使用，每帧重叠会系统性多算固定区像素（首个测试版本全部高度断言
失败的根因）。

### 5. Android 采集链路

```
MainActivity → MediaProjection 授权 → CaptureService(前台, type=mediaProjection)
  → VirtualDisplay + ImageReader(RGBA, maxImages=4)
  → 主线程 acquireLatestImage（节流 125ms，行跨度对齐拷贝）
  → worker: plan.add_frame + JPEG 落盘（quality 92）
  → nativeIsLastDuplicate 连续 10 帧 → finishCapture
  → FrameComposer: 重建 plan 入帧 → compose 回调逐帧解码 JPEG → Bitmap
```

内存峰值 = 1 帧 RGBA（~10MB）+ 计划灰度图（帧数×90KB）+ 输出长图。

### 6. iOS Broadcast 链路与内存约束

- 扩展进程内存限额约 50MB：`processVideoSampleBuffer` 收到帧立即交给
  `AVAssetWriterInputPixelBufferAdaptor` 硬编码 H.264 写入 App Group，
  输入未就绪则丢帧（录屏帧率高，不影响拼接）。
- 主 App 与扩展无法直接通信：扩展 `broadcastStarted/Finished` 维护
  `recording.flag`，录制完成后视频落在 `pending/`；主 App 1.5s 轮询。
- 主 App 处理：抽帧 8fps → JPEG 落盘临时目录 + 入计划 → compose 按文件
  解码合成 → 删除临时目录、视频移入 `processed/`（保留最近 3 个）。

### 7. 测试策略（core/tests）

合成"长页面 + 模拟滚动截帧"夹具：确定性哈希纹理（含每页独有的地标行，
防止不同页面因同构布局产生周期性互相关），可注入噪声/动态区域/重复帧。

覆盖：基础拼接、自动固定区检测、动态内容投票、重复帧丢弃、页底检测、
慢速滚动（26 帧小步长）、噪声鲁棒、两帧、gap、流式与一次性结果逐字节
一致、参数校验。本仓库已在 Windows (g++ 15.1) 全部通过。
