package com.stitchly.app.capture

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File

/** 截屏会话全局状态（UI 与服务共享） */
object CaptureSession {
    sealed class State {
        data object Idle : State()
        data class Capturing(val frames: Int) : State()
        data class Composing(val frames: Int) : State()
        data class Done(val resultPath: String, val width: Int, val height: Int) : State()
        data class Error(val message: String) : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    /** 是否启用无障碍自动滚动（Home 页开关） */
    @Volatile
    var autoScroll: Boolean = false

    fun update(s: State) {
        _state.value = s
    }

    fun reset() {
        _state.value = State.Idle
    }
}
