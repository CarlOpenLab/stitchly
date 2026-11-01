package com.longshot.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.longshot.app.capture.CaptureService
import com.longshot.app.capture.CaptureSession

@Composable
fun CaptureScreen() {
    val context = LocalContext.current
    val state by CaptureSession.state.collectAsState()

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            when (val s = state) {
                is CaptureSession.State.Capturing -> {
                    Text("正在采集 · 已捕获 ${s.frames} 帧", style = MaterialTheme.typography.titleLarge)
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "请切换到目标应用并缓慢滚动页面\n停止滚动约 1 秒后自动结束",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Spacer(Modifier.height(24.dp))
                    Button(onClick = { CaptureService.stop(context) }) {
                        Text("停止采集")
                    }
                }
                is CaptureSession.State.Composing -> {
                    CircularProgressIndicator()
                    Spacer(Modifier.height(16.dp))
                    Text("正在拼接 ${s.frames} 帧…", style = MaterialTheme.typography.titleMedium)
                }
                else -> {
                    Text("会话已结束", style = MaterialTheme.typography.titleMedium)
                }
            }
        }
    }
}
