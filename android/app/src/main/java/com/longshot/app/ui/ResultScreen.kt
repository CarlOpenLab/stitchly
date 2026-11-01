package com.longshot.app.ui

import android.content.Intent
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.weight
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.longshot.app.capture.CaptureSession
import com.longshot.app.util.ImageIo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
fun ResultScreen(onDone: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbar = remember { SnackbarHostState() }
    val state by CaptureSession.state.collectAsState()
    var bmp by remember { mutableStateOf<android.graphics.Bitmap?>(null) }

    LaunchedEffect(state) {
        val done = state as? CaptureSession.State.Done ?: return@LaunchedEffect
        withContext(Dispatchers.IO) { BitmapFactory.decodeFile(done.resultPath) }
            ?.let { bmp = it }
    }

    Scaffold(snackbarHost = { SnackbarHost(snackbar) }) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            val b = bmp
            if (b != null) {
                Image(
                    bitmap = b.asImageBitmap(),
                    contentDescription = "拼接结果",
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    contentScale = ContentScale.Fit,
                )
            } else {
                Text("结果加载中…", style = MaterialTheme.typography.bodyMedium)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(
                    onClick = {
                        val bitmap = bmp ?: return@Button
                        scope.launch {
                            val saved = withContext(Dispatchers.IO) {
                                ImageIo.saveToGallery(context, bitmap, "longshot-${System.currentTimeMillis()}.png")
                            }
                            snackbar.showSnackbar(if (saved != null) "已保存到相册 Pictures/Longshot" else "保存失败")
                        }
                    },
                    modifier = Modifier.weight(1f),
                ) { Text("保存到相册") }
                OutlinedButton(
                    onClick = {
                        val bitmap = bmp ?: return@OutlinedButton
                        scope.launch {
                            val uri = withContext(Dispatchers.IO) {
                                ImageIo.saveForShare(context, bitmap, "longshot.png")
                            }
                            if (uri != null) {
                                val send = Intent(Intent.ACTION_SEND).apply {
                                    type = "image/png"
                                    putExtra(Intent.EXTRA_STREAM, uri)
                                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                                }
                                context.startActivity(Intent.createChooser(send, "分享长图"))
                            }
                        }
                    },
                    modifier = Modifier.weight(1f),
                ) { Text("分享") }
                OutlinedButton(
                    onClick = {
                        bmp?.recycle()
                        bmp = null
                        onDone()
                    },
                    modifier = Modifier.weight(1f),
                ) { Text("完成") }
            }
        }
    }
}
