package com.stitchly.app.ui

import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.stitchly.app.capture.CaptureSession

@Composable
fun HomeScreen(onCapture: () -> Unit, onAlbum: () -> Unit) {
    val context = LocalContext.current
    var autoScroll by remember { mutableStateOf(CaptureSession.autoScroll) }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(Modifier.height(24.dp))
            Text("长截图", style = MaterialTheme.typography.headlineLarge)
            Text(
                "系统级长截图工具：实时抓帧拼接 / 相册拼接",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(24.dp))

            Button(onClick = onCapture, modifier = Modifier.fillMaxWidth()) {
                Text("开始实时长截图")
            }
            Text(
                "开始后请切换到目标应用并缓慢滚动；停止滚动约 1 秒后自动结束采集并拼接。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Button(onClick = onAlbum, modifier = Modifier.fillMaxWidth()) {
                Text("相册截图拼接")
            }
            Text(
                "选择同一页面连续截取的多张截图，自动检测重叠拼成长图。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(Modifier.height(12.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(
                    checked = autoScroll,
                    onCheckedChange = {
                        CaptureSession.autoScroll = it
                        autoScroll = it
                    },
                )
                Text("自动滚动（可选，需开启无障碍）")
            }
            OutlinedButton(onClick = {
                context.startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            }) {
                Text("打开无障碍设置")
            }
        }
    }
}
