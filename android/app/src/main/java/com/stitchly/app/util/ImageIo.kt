package com.stitchly.app.util

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.core.content.FileProvider
import java.io.File
import java.nio.ByteBuffer

object ImageIo {

    /** RGBA 字节 → Bitmap（Android Bitmap 内存序为 RGBA_8888） */
    fun rgbaToBitmap(rgba: ByteArray, w: Int, h: Int): Bitmap {
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val buffer = ByteBuffer.wrap(rgba)
        bmp.copyPixelsFromBuffer(buffer)
        return bmp
    }

    fun bitmapToRgba(bmp: Bitmap): ByteArray {
        val src = if (bmp.config == Bitmap.Config.ARGB_8888) bmp
        else bmp.copy(Bitmap.Config.ARGB_8888, false)
        val buffer = ByteBuffer.allocate(src.rowBytes * src.height)
        src.copyPixelsFromBuffer(buffer)
        val out = buffer.array()
        if (src !== bmp) src.recycle()
        return out
    }

    /** 解码文件为指定尺寸 RGBA */
    fun decodeToRgba(file: File, w: Int, h: Int): ByteArray? {
        val src = BitmapFactory.decodeFile(file.absolutePath) ?: return null
        return scaledRgba(src, w, h)
    }

    /** 解码 Uri 为指定尺寸 RGBA */
    fun decodeUriToRgba(ctx: Context, uri: Uri, w: Int, h: Int): ByteArray? {
        val src = ctx.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it) }
            ?: return null
        return scaledRgba(src, w, h)
    }

    fun imageSizeOf(ctx: Context, uri: Uri): Pair<Int, Int>? {
        val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        ctx.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
            ?: return null
        if (opts.outWidth <= 0 || opts.outHeight <= 0) return null
        return opts.outWidth to opts.outHeight
    }

    private fun scaledRgba(src: Bitmap, w: Int, h: Int): ByteArray {
        val bmp = if (src.width != w || src.height != h)
            Bitmap.createScaledBitmap(src, w, h, true) else src
        val out = bitmapToRgba(bmp)
        if (bmp !== src) bmp.recycle()
        src.recycle()
        return out
    }

    /** 保存到公共图库 Pictures/Longshot */
    fun saveToGallery(ctx: Context, bmp: Bitmap, displayName: String): Uri? {
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
            put(MediaStore.Images.Media.MIME_TYPE, "image/png")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/Longshot")
            }
        }
        val uri = ctx.contentResolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
            ?: return null
        ctx.contentResolver.openOutputStream(uri)?.use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return uri
    }

    /** 拷贝到 cache/shared 供分享 */
    fun saveForShare(ctx: Context, bmp: Bitmap, displayName: String): Uri? {
        val dir = File(ctx.cacheDir, "shared").apply { mkdirs() }
        val f = File(dir, displayName)
        f.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return FileProvider.getUriForFile(ctx, ctx.packageName + ".fileprovider", f)
    }
}
