package com.longshot.app.capture

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.Image
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.util.DisplayMetrics
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.core.content.ServiceCompat
import com.longshot.app.R
import com.longshot.app.StitchEngine
import com.longshot.app.stitch.FrameComposer
import com.longshot.app.util.ImageIo
import java.io.File
import java.util.concurrent.Executors

/**
 * 实时长截图采集服务：
 * MediaProjection + ImageReader 抓帧（节流 ~8fps）→ 流式加入拼接计划 +
 * 帧落盘 JPEG；连续重复帧（滚动停止/到底）自动结束，随后流式合成。
 */
class CaptureService : Service() {

    companion object {
        const val EXTRA_RESULT_CODE = "resultCode"
        const val EXTRA_DATA = "data"
        private const val ACTION_STOP = "com.longshot.app.action.STOP"
        private const val CHANNEL_ID = "capture"
        private const val NOTIF_ID = 1
        private const val CAPTURE_INTERVAL_MS = 125L // ~8fps
        private const val IDLE_FRAMES_TO_STOP = 10   // 连续 ~1.2s 无新内容
        private const val MAX_FRAMES = 400

        @Volatile
        var isRunning = false
            private set

        fun start(ctx: Context, resultCode: Int, data: Intent) {
            val i = Intent(ctx, CaptureService::class.java)
                .putExtra(EXTRA_RESULT_CODE, resultCode)
                .putExtra(EXTRA_DATA, data)
            ContextCompat.startForegroundService(ctx, i)
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, CaptureService::class.java))
        }
    }

    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var reader: android.media.ImageReader? = null
    private val worker = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    private var plan = 0L
    private var frameW = 0
    private var frameH = 0
    private var lastTs = 0L
    private var idleCount = 0
    private var frameCount = 0
    private var stopping = false
    private var overlayView: View? = null
    private lateinit var framesDir: File

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        isRunning = true
        val channel = NotificationChannel(CHANNEL_ID, getString(R.string.notif_channel_capture),
            NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            finishCapture()
            return START_NOT_STICKY
        }
        if (projection != null) return START_NOT_STICKY

        val data: Intent = intent?.getParcelableExtra(EXTRA_DATA) ?: run {
            stopSelf()
            return START_NOT_STICKY
        }
        val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, -1)

        startForegroundWithType()

        val metrics: DisplayMetrics = resources.displayMetrics
        frameW = metrics.widthPixels
        frameH = metrics.heightPixels
        framesDir = File(cacheDir, "frames-${System.currentTimeMillis()}").apply { mkdirs() }
        CaptureSession.update(CaptureSession.State.Capturing(0))

        plan = StitchEngine.nativeCreatePlan(frameW, frameH, 0, 0, true)

        val r = android.media.ImageReader.newInstance(frameW, frameH, PixelFormat.RGBA_8888, 4)
        reader = r
        val pm = getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        projection = pm.getMediaProjection(resultCode, data)
        projection?.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() = finishCapture()
        }, mainHandler)

        virtualDisplay = projection?.createVirtualDisplay(
            "longshot", frameW, frameH, metrics.densityDpi,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, r.surface, null, mainHandler,
        )
        r.setOnImageAvailableListener({ onImageAvailable(it) }, mainHandler)
        addOverlay()
        return START_NOT_STICKY
    }

    private fun onImageAvailable(ir: android.media.ImageReader) {
        if (stopping || plan == 0L) return
        val now = SystemClock.uptimeMillis()
        val img: Image = ir.acquireLatestImage() ?: return
        if (now - lastTs < CAPTURE_INTERVAL_MS) {
            img.close()
            return
        }
        lastTs = now
        val rgba = imageToRgba(img)
        img.close()
        if (rgba != null) worker.execute { processFrame(rgba) }
    }

    /** Image planes 的行跨度可能与宽度*4 不同，逐行拷贝为紧凑 RGBA */
    private fun imageToRgba(img: Image): ByteArray? {
        val plane = img.planes[0]
        val rowStride = plane.rowStride
        val buf = plane.buffer
        val rowBytes = frameW * 4
        if (buf.capacity() < rowStride * (frameH - 1) + rowBytes) return null
        val out = ByteArray(rowBytes * frameH)
        for (y in 0 until frameH) {
            buf.position(y * rowStride)
            buf.limit(y * rowStride + rowBytes)
            buf.get(out, y * rowBytes, rowBytes)
        }
        buf.clear()
        return out
    }

    private fun processFrame(rgba: ByteArray) {
        if (stopping || plan == 0L) return
        if (!StitchEngine.nativeAddFrame(plan, rgba)) return
        frameCount++

        // 帧落盘，供合成阶段按需重新解码
        val bmp = ImageIo.rgbaToBitmap(rgba, frameW, frameH)
        val f = File(framesDir, "frame_%05d.jpg".format(frameCount))
        f.outputStream().use { bmp.compress(Bitmap.CompressFormat.JPEG, 92, it) }
        bmp.recycle()

        if (StitchEngine.nativeIsLastDuplicate(plan)) idleCount++ else idleCount = 0
        CaptureSession.update(CaptureSession.State.Capturing(frameCount))
        if (idleCount >= IDLE_FRAMES_TO_STOP || frameCount >= MAX_FRAMES) finishCapture()
    }

    /** 结束采集并进入流式合成 */
    private fun finishCapture() {
        if (stopping) return
        stopping = true
        mainHandler.post {
            removeOverlay()
            try { virtualDisplay?.release() } catch (_: Exception) {}
            try { reader?.close() } catch (_: Exception) {}
            try { projection?.stop() } catch (_: Exception) {}
            virtualDisplay = null
            reader = null
            projection = null

            CaptureSession.update(CaptureSession.State.Composing(frameCount))
            worker.execute {
                val bmp = if (frameCount >= 2)
                    FrameComposer.composeCaptureDir(applicationContext, framesDir, frameW, frameH)
                else null
                if (bmp != null) {
                    val out = File(cacheDir, "longshot-result.png")
                    out.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
                    CaptureSession.update(
                        CaptureSession.State.Done(out.absolutePath, bmp.width, bmp.height),
                    )
                    bmp.recycle()
                    framesDir.deleteRecursively()
                } else {
                    CaptureSession.update(
                        CaptureSession.State.Error("有效帧不足，未生成长图（请截取同一页面连续滚动的内容）"),
                    )
                    framesDir.deleteRecursively()
                }
                stopSelf()
            }
        }
    }

    private fun startForegroundWithType() {
        val stopPi = PendingIntent.getService(
            this, 0,
            Intent(this, CaptureService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val notif: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getString(R.string.notif_capturing))
            .addAction(0, getString(R.string.stop_action), stopPi)
            .setOngoing(true)
            .build()
        ServiceCompat.startForeground(
            this, NOTIF_ID, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION,
        )
    }

    /** 悬浮"停止"按钮（需悬浮窗权限，缺省时仅用通知栏停止） */
    private fun addOverlay() {
        if (!Settings.canDrawOverlays(this)) return
        val wm = getSystemService(WINDOW_SERVICE) as WindowManager
        val btn = Button(this).apply {
            text = getString(R.string.stop_action)
            setOnClickListener { finishCapture() }
        }
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT,
        )
        params.gravity = Gravity.END or Gravity.TOP
        params.y = 120
        try {
            wm.addView(btn, params)
            overlayView = btn
        } catch (_: Exception) {
        }
    }

    private fun removeOverlay() {
        overlayView?.let {
            try {
                (getSystemService(WINDOW_SERVICE) as WindowManager).removeView(it)
            } catch (_: Exception) {
            }
        }
        overlayView = null
    }

    override fun onDestroy() {
        isRunning = false
        if (plan != 0L) {
            StitchEngine.nativeFreePlan(plan)
            plan = 0
        }
        worker.shutdown()
        removeOverlay()
        super.onDestroy()
    }
}
