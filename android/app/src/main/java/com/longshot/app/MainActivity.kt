package com.longshot.app

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import com.longshot.app.capture.CaptureService
import com.longshot.app.capture.CaptureSession
import com.longshot.app.ui.AlbumScreen
import com.longshot.app.ui.CaptureScreen
import com.longshot.app.ui.HomeScreen
import com.longshot.app.ui.ResultScreen

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = lightColorScheme()) {
                AppRoot()
            }
        }
    }
}

@Composable
fun AppRoot() {
    val context = androidx.compose.ui.platform.LocalContext.current
    var route by remember { mutableStateOf("home") }
    val sessionState by CaptureSession.state.collectAsState()

    val projectionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            CaptureService.start(context, result.resultCode, result.data!!)
            route = "capture"
        }
    }

    val launchProjection: () -> Unit = {
        val mpm = context.getSystemService(Context.MEDIA_PROJECTION_SERVICE)
            as MediaProjectionManager
        projectionLauncher.launch(mpm.createScreenCaptureIntent())
    }

    // API 33+ 先请求通知权限，再进入录屏授权
    val notifLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { launchProjection() }

    // 会话完成/出错时切换页面
    LaunchedEffect(sessionState) {
        when (sessionState) {
            is CaptureSession.State.Done -> route = "result"
            is CaptureSession.State.Error -> {
                CaptureSession.reset()
                route = "home"
            }
            else -> Unit
        }
    }

    when (route) {
        "home" -> HomeScreen(
            onCapture = {
                if (Build.VERSION.SDK_INT >= 33 &&
                    ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED
                ) {
                    notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                } else {
                    launchProjection()
                }
            },
            onAlbum = { route = "album" },
        )
        "capture" -> CaptureScreen()
        "album" -> AlbumScreen(onBack = { route = "home" })
        "result" -> ResultScreen(onDone = {
            CaptureSession.reset()
            route = "home"
        })
    }
}
