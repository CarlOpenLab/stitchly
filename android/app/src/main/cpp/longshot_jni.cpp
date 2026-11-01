// Longshot JNI 桥接：Kotlin <-> C++ core
#include <jni.h>

#include <cstdint>
#include <vector>

#include "longshot/stitch.h"

namespace {

jobject make_result(JNIEnv* env, const LsResult* r) {
    jclass cls = env->FindClass("com/stitchly/app/StitchResult");
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "([BII[I[F[Z[ZIZI)V");
    if (!ctor) return nullptr;

    const jsize n = r->frame_count;
    const jsize px = (jsize)r->width * r->height * 4;

    jbyteArray data = env->NewByteArray(px);
    jintArray offsets = env->NewIntArray(n);
    jfloatArray scores = env->NewFloatArray(n);
    jbooleanArray dropped = env->NewBooleanArray(n);
    jbooleanArray gaps = env->NewBooleanArray(n);
    if (!data || !offsets || !scores || !dropped || !gaps) return nullptr;

    env->SetByteArrayRegion(data, 0, px, (const jbyte*)r->data);

    std::vector<jint> off(n);
    std::vector<jfloat> sc(n);
    std::vector<jboolean> dr(n), gp(n);
    for (jsize i = 0; i < n; i++) {
        off[i] = r->frames[i].offset_y;
        sc[i] = r->frames[i].score;
        dr[i] = r->frames[i].dropped ? JNI_TRUE : JNI_FALSE;
        gp[i] = r->frames[i].gap ? JNI_TRUE : JNI_FALSE;
    }
    env->SetIntArrayRegion(offsets, 0, n, off.data());
    env->SetFloatArrayRegion(scores, 0, n, sc.data());
    env->SetBooleanArrayRegion(dropped, 0, n, dr.data());
    env->SetBooleanArrayRegion(gaps, 0, n, gp.data());

    return env->NewObject(cls, ctor, data, (jint)r->width, (jint)r->height,
                          offsets, scores, dropped, gaps,
                          (jboolean)(r->reached_bottom ? JNI_TRUE : JNI_FALSE),
                          (jint)r->dropped_count, (jint)r->gap_count);
}

// ---- 合成阶段按需取帧的回调 ----
struct ProviderCtx {
    JNIEnv* env = nullptr;
    jobject provider = nullptr;
    jmethodID get = nullptr;
    jint width = 0, height = 0;
    std::vector<uint8_t> buf; // 复用的 RGBA 缓冲（回调返回后 core 立即拷贝行数据）
};

int32_t provider_trampoline(int32_t index, LsImage* out, void* user) {
    auto* ctx = (ProviderCtx*)user;
    auto arr = (jbyteArray)ctx->env->CallObjectMethod(ctx->provider, ctx->get, (jint)index);
    if (ctx->env->ExceptionCheck()) {
        ctx->env->ExceptionClear();
        return -1;
    }
    if (!arr) return -1;
    const jsize len = ctx->env->GetArrayLength(arr);
    const size_t need = (size_t)ctx->width * ctx->height * 4;
    if (len != (jsize)need) {
        ctx->env->DeleteLocalRef(arr);
        return -1;
    }
    ctx->buf.resize(need);
    ctx->env->GetByteArrayRegion(arr, 0, len, (jbyte*)ctx->buf.data());
    ctx->env->DeleteLocalRef(arr);
    out->data = ctx->buf.data();
    out->width = ctx->width;
    out->height = ctx->height;
    out->stride = 0;
    return LS_OK;
}

LsOptions make_options(jint cropTop, jint cropBottom, jboolean autoChrome) {
    LsOptions opt;
    ls_options_init(&opt);
    opt.crop_top = cropTop;
    opt.crop_bottom = cropBottom;
    opt.auto_detect_chrome = autoChrome ? 1 : 0;
    return opt;
}

} // namespace

extern "C" {

// ---- 一次性拼接（相册模式） ----
JNIEXPORT jobject JNICALL
Java_com_longshot_app_StitchEngine_stitchImages(JNIEnv* env, jobject, jobjectArray frames,
                                                jint width, jint height,
                                                jint cropTop, jint cropBottom,
                                                jboolean autoChrome) {
    if (!frames || width < 16 || height < 16) return nullptr;
    const jsize n = env->GetArrayLength(frames);
    if (n < 1) return nullptr;
    const size_t need = (size_t)width * height * 4;

    std::vector<LsImage> imgs(n);
    std::vector<jbyteArray> arrs(n, nullptr);
    std::vector<const jbyte*> pins(n, nullptr);

    for (jsize i = 0; i < n; i++) {
        auto arr = (jbyteArray)env->GetObjectArrayElement(frames, i);
        if (!arr || (size_t)env->GetArrayLength(arr) != need) {
            if (arr) env->DeleteLocalRef(arr);
            for (jsize k = 0; k < i; k++)
                env->ReleaseByteArrayElements(arrs[k], (jbyte*)pins[k], JNI_ABORT);
            return nullptr;
        }
        pins[i] = env->GetByteArrayElements(arr, nullptr);
        arrs[i] = arr;
        imgs[i].data = (const uint8_t*)pins[i];
        imgs[i].width = width;
        imgs[i].height = height;
        imgs[i].stride = 0;
    }

    const LsOptions opt = make_options(cropTop, cropBottom, autoChrome);
    LsResult* r = nullptr;
    const int32_t rc = ls_stitch_images(imgs.data(), n, &opt, &r);

    for (jsize i = 0; i < n; i++) {
        env->ReleaseByteArrayElements(arrs[i], (jbyte*)pins[i], JNI_ABORT);
        env->DeleteLocalRef(arrs[i]);
    }
    if (rc != LS_OK || !r) return nullptr;

    jobject jres = make_result(env, r);
    ls_result_free(r);
    return jres;
}

// ---- 流式拼接 ----
JNIEXPORT jlong JNICALL
Java_com_longshot_app_StitchEngine_nativeCreatePlan(JNIEnv*, jobject,
                                                    jint width, jint height,
                                                    jint cropTop, jint cropBottom,
                                                    jboolean autoChrome) {
    if (width < 16 || height < 16) return 0;
    const LsOptions opt = make_options(cropTop, cropBottom, autoChrome);
    LsPlan* p = ls_plan_create(width, height, &opt);
    return p ? (jlong)(intptr_t)p : 0;
}

JNIEXPORT jboolean JNICALL
Java_com_longshot_app_StitchEngine_nativeAddFrame(JNIEnv* env, jobject, jlong plan,
                                                  jbyteArray rgba) {
    auto* p = (LsPlan*)(intptr_t)plan;
    if (!p || !rgba) return JNI_FALSE;
    const jsize len = env->GetArrayLength(rgba);
    if ((size_t)len != (size_t)p->full_w * p->full_h * 4) return JNI_FALSE;
    jbyte* data = env->GetByteArrayElements(rgba, nullptr);
    LsImage img{data, p->full_w, p->full_h, 0};
    const int32_t rc = ls_plan_add_frame(p, &img);
    env->ReleaseByteArrayElements(rgba, data, JNI_ABORT);
    return rc == LS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_longshot_app_StitchEngine_nativeIsLastDuplicate(JNIEnv*, jobject, jlong plan) {
    auto* p = (LsPlan*)(intptr_t)plan;
    if (!p) return JNI_FALSE;
    return ls_plan_last_frame_duplicate(p) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_com_longshot_app_StitchEngine_nativeCompose(JNIEnv* env, jobject, jlong plan,
                                                 jobject provider) {
    auto* p = (LsPlan*)(intptr_t)plan;
    if (!p || !provider) return nullptr;

    jclass cls = env->GetObjectClass(provider);
    jmethodID get = env->GetMethodID(cls, "getFrame", "(I)[B");
    if (!get) return nullptr;

    ProviderCtx ctx;
    ctx.env = env;
    ctx.provider = provider;
    ctx.get = get;
    ctx.width = p->full_w;
    ctx.height = p->full_h;

    LsResult* r = nullptr;
    const int32_t rc = ls_plan_compose(p, provider_trampoline, &ctx, &r);
    if (rc != LS_OK || !r) return nullptr;

    jobject jres = make_result(env, r);
    ls_result_free(r);
    return jres;
}

JNIEXPORT void JNICALL
Java_com_longshot_app_StitchEngine_nativeFreePlan(JNIEnv*, jobject, jlong plan) {
    delete (LsPlan*)(intptr_t)plan;
}

} // extern "C"
