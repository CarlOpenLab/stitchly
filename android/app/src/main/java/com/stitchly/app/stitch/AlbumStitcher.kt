package com.stitchly.app.stitch

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import com.stitchly.app.StitchEngine
import com.stitchly.app.util.ImageIo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** 相册截图拼接（一次性 core API） */
object AlbumStitcher {

    /** 按用户选图的顺序拼接；截图应为同一页面连续滚动的一组 */
    suspend fun stitch(ctx: Context, uris: List<Uri>): Bitmap? = withContext(Dispatchers.Default) {
        if (uris.size < 2) return@withContext null
        val (w, h) = ImageIo.imageSizeOf(ctx, uris[0]) ?: return@withContext null

        val frames = ArrayList<ByteArray>(uris.size)
        for (u in uris) {
            val rgba = ImageIo.decodeUriToRgba(ctx, u, w, h) ?: return@withContext null
            frames.add(rgba)
        }

        val result = StitchEngine.stitchImages(frames.toTypedArray(), w, h, 0, 0, true)
            ?: return@withContext null
        ImageIo.rgbaToBitmap(result.pixels, result.width, result.height)
    }
}
