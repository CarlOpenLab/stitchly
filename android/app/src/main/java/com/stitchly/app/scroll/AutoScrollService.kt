package com.stitchly.app.scroll

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.os.Handler
import android.os.Looper
import android.view.accessibility.AccessibilityEvent
import com.stitchly.app.capture.CaptureService
import com.stitchly.app.capture.CaptureSession

/**
 * 可选增强：采集期间自动模拟缓慢上滑手势（需用户在系统设置中开启本服务，
 * 且在应用内打开"自动滚动"开关）。服务不读取任何屏幕内容，仅发送手势。
 */
class AutoScrollService : AccessibilityService() {

    private val handler = Handler(Looper.getMainLooper())
    private var looping = false

    private val loop = object : Runnable {
        override fun run() {
            if (!looping) return
            if (!CaptureService.isRunning || !CaptureSession.autoScroll) {
                handler.postDelayed(this, 500) // 会话未开始，低频待命
                return
            }
            val m = resources.displayMetrics
            val path = Path().apply {
                moveTo(m.widthPixels / 2f, m.heightPixels * 0.72f)
                lineTo(m.widthPixels / 2f, m.heightPixels * 0.30f)
            }
            val gesture = GestureDescription.Builder()
                .addStroke(GestureDescription.StrokeDescription(path, 0, 650))
                .build()
            dispatchGesture(gesture, null, null)
            handler.postDelayed(this, 900)
        }
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        looping = true
        handler.postDelayed(loop, 400)
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit

    override fun onInterrupt() = Unit

    override fun onDestroy() {
        looping = false
        handler.removeCallbacks(loop)
        super.onDestroy()
    }
}
