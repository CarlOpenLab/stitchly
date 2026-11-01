# JNI 方法按名称绑定，保留 StitchEngine 的 native 方法
-keepclasseswithmembernames class com.stitchly.app.StitchEngine {
    native <methods>;
}
-keep class com.stitchly.app.StitchEngine$StitchResult { *; }
-keep class com.stitchly.app.StitchEngine$FrameProvider { *; }
