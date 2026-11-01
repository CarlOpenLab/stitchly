package com.longshot.app.stitch

import android.graphics.Bitmap
import com.longshot.app.StitchEngine
import com.longshot.app.util.ImageIo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/** 把采集的帧目录（JPEG）合成为长图 Bitmap */
object FrameComposer {

    /**
     * 流式流程：先建立计划并加入所有帧（core 内部仅存缩小灰度图），
     * 再通过回调逐帧从磁盘解码全分辨率数据合成。
     */
    suspend fun composeCaptureDir(dir: File, width: Int, height: Int): Bitmap? =
        withContext(Dispatchers.Default) {
            val files = dir.listFiles { f -> f.name.startsWith("frame_") }
                ?.sortedBy { it.name }
                ?.takeIf { it.size >= 2 }
                ?: return@withContext null

            val plan = StitchEngine.nativeCreatePlan(width, height, 0, 0, true)
            if (plan == 0L) return@withContext null
            try {
                for (f in files) {
                    val rgba = ImageIo.decodeToRgba(f, width, height) ?: continue
                    if (!StitchEngine.nativeAddFrame(plan, rgba)) break
                }
                val result = StitchEngine.nativeCompose(plan) { index ->
                    files.getOrNull(index)?.let { ImageIo.decodeToRgba(it, width, height) }
                } ?: return@withContext null

                ImageIo.rgbaToBitmap(result.pixels, result.width, result.height)
            } finally {
                StitchEngine.nativeFreePlan(plan)
            }
        }
}
