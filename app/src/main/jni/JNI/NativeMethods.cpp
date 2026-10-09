#include "NativeMethods.h"
#include "../Core/Runtime.h"
#include "../UI/Renderer.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace jni {

static void JNICALL impl_nativeOnSurfaceCreated(JNIEnv* env, jclass clazz) {
    LOGI("nativeOnSurfaceCreated");
    Renderer::instance().onSurfaceCreated();
}

static void JNICALL impl_nativeOnSurfaceChanged(JNIEnv* env, jclass clazz,
                                                 jint width, jint height) {
    LOGI("nativeOnSurfaceChanged %dx%d", (int)width, (int)height);
    Renderer::instance().onSurfaceChanged((int)width, (int)height);
}

static void JNICALL impl_nativeOnDrawFrame(JNIEnv* env, jclass clazz,
                                            jint width, jint height) {
    Renderer::instance().onDrawFrame((int)width, (int)height);
}

static void JNICALL impl_nativeOnTouch(JNIEnv* env, jclass clazz,
                                        jint action, jfloat x, jfloat y,
                                        jint pointerId) {
    Renderer::instance().onTouch((int)action, (float)x, (float)y, (int)pointerId);
}

static void JNICALL impl_nativeOnKey(JNIEnv* env, jclass clazz,
                                      jint keyCode, jint action) {
    (void)env; (void)clazz; (void)keyCode; (void)action;
}

static const JNINativeMethod g_methods[] = {
    {"nativeOnSurfaceCreated", "()V", (void*)impl_nativeOnSurfaceCreated},
    {"nativeOnSurfaceChanged", "(II)V", (void*)impl_nativeOnSurfaceChanged},
    {"nativeOnDrawFrame", "(II)V", (void*)impl_nativeOnDrawFrame},
    {"nativeOnTouch", "(IFFI)V", (void*)impl_nativeOnTouch},
};

bool registerNativeMethods(JNIEnv* env, jclass modViewClass) {
    if (!env || !modViewClass) return false;
    jint n = sizeof(g_methods) / sizeof(g_methods[0]);
    if (env->RegisterNatives(modViewClass, g_methods, n) != JNI_OK) {
        LOGE("RegisterNatives failed");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    LOGI("RegisterNatives OK (%d methods)", (int)n);
    return true;
}

void onSurfaceCreated(JNIEnv* env, jclass clazz) { impl_nativeOnSurfaceCreated(env, clazz); }
void onSurfaceChanged(JNIEnv* env, jclass clazz, jint w, jint h) { impl_nativeOnSurfaceChanged(env, clazz, w, h); }
void onDrawFrame(JNIEnv* env, jclass clazz, jint w, jint h) { impl_nativeOnDrawFrame(env, clazz, w, h); }
void onTouch(JNIEnv* env, jclass clazz, jint a, jfloat x, jfloat y, jint p) { impl_nativeOnTouch(env, clazz, a, x, y, p); }
void onKey(JNIEnv* env, jclass clazz, jint k, jint a) { impl_nativeOnKey(env, clazz, k, a); }

} // namespace jni
} // namespace yamgg
