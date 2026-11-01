package com.stitchly.app

/** 拼接结果（对应 core 的 LsResult） */
class StitchResult(
    val pixels: ByteArray, // RGBA8888，行优先
    val width: Int,
    val height: Int,
    val offsets: IntArray, // 每帧内容在输出中的起始 y；丢弃帧为 -1
    val scores: FloatArray,
    val dropped: BooleanArray,
    val gaps: BooleanArray,
    val reachedBottom: Boolean,
    val droppedCount: Int,
    val gapCount: Int,
)

/** 合成时按需提供全分辨率帧（按输入帧下标，可从磁盘解码） */
fun interface FrameProvider {
    fun getFrame(index: Int): ByteArray?
}

object StitchEngine {
    init {
        System.loadLibrary("longshot")
    }

    /** 一次性拼接（相册模式）：所有帧常驻内存，适合少量截图 */
    external fun stitchImages(
        frames: Array<ByteArray>,
        width: Int,
        height: Int,
        cropTop: Int,
        cropBottom: Int,
        autoChrome: Boolean,
    ): StitchResult?

    /** 流式拼接（录屏/实时抓帧模式）：计划内部只保留缩小灰度图 */
    external fun nativeCreatePlan(
        width: Int,
        height: Int,
        cropTop: Int,
        cropBottom: Int,
        autoChrome: Boolean,
    ): Long

    external fun nativeAddFrame(plan: Long, rgba: ByteArray): Boolean

    /** 最后一帧是否与前一帧几乎相同（滚动停止的快速判定） */
    external fun nativeIsLastDuplicate(plan: Long): Boolean

    /** 对齐并合成；getFrame 会按需回调取全分辨率帧 */
    external fun nativeCompose(plan: Long, provider: FrameProvider): StitchResult?

    external fun nativeFreePlan(plan: Long)
}
