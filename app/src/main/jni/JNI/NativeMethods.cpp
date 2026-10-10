#include "NativeMethods.h"
#include "../Core/Runtime.h"
#include "../UI/Renderer.h"
#include "../UI/MainWindow.h"
#include "../UI/Tabs/FileBrowser.h"

#include <android/log.h>
#include <vector>
#include <climits>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace jni {

// ─── forward decls of the C bridges (defined in Main.cpp) ───
extern "C" int  yamgg_popPendingCmd(char* out, int maxLen);
extern "C" int  yamgg_peekPendingCmdSize(void);

static void JNICALL impl_nativeOnSurfaceCreated(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    LOGI("nativeOnSurfaceCreated");
    Renderer::instance().onSurfaceCreated();
}

static void JNICALL impl_nativeOnSurfaceChanged(JNIEnv* env, jclass clazz,
                                                 jint width, jint height) {
    (void)env; (void)clazz;
    LOGI("nativeOnSurfaceChanged %dx%d", (int)width, (int)height);
    Renderer::instance().onSurfaceChanged((int)width, (int)height);
}

static void JNICALL impl_nativeOnDrawFrame(JNIEnv* env, jclass clazz,
                                            jint width, jint height) {
    (void)env; (void)clazz;
    Renderer::instance().onDrawFrame((int)width, (int)height);
}

static void JNICALL impl_nativeOnTouch(JNIEnv* env, jclass clazz,
                                        jint action, jfloat x, jfloat y,
                                        jint pointerId) {
    (void)env; (void)clazz;
    Renderer::instance().onTouch((int)action, (float)x, (float)y, (int)pointerId);
}

static void JNICALL impl_nativeOnKey(JNIEnv* env, jclass clazz,
                                      jint keyCode, jint action) {
    (void)env; (void)clazz;
    Renderer::instance().onKey((int)keyCode, (int)action);
}

static void JNICALL impl_nativeOnChar(JNIEnv* env, jclass clazz,
                                       jint codepoint) {
    (void)env; (void)clazz;
    Renderer::instance().onChar((unsigned int)codepoint);
}

static void JNICALL impl_nativeOnScroll(JNIEnv* env, jclass clazz,
                                         jfloat dx, jfloat dy) {
    (void)env; (void)clazz;
    Renderer::instance().onScroll((float)dx, (float)dy);
}

static jboolean JNICALL impl_nativeWantCaptureMouse(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return Renderer::instance().wantCaptureMouse() ? JNI_TRUE : JNI_FALSE;
}

static jboolean JNICALL impl_nativeWantTextInput(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return Renderer::instance().wantTextInput() ? JNI_TRUE : JNI_FALSE;
}

static jboolean JNICALL impl_nativeHitTest(JNIEnv* env, jclass clazz,
                                            jfloat x, jfloat y) {
    (void)env; (void)clazz;
    float fx = (float)x, fy = (float)y;

    // Priority: FileBrowser (top-most) → MainWindow (or minimized chip).
    if (FileBrowser::instance().hitTest(fx, fy)) return JNI_TRUE;
    if (MainWindow::instance().hitTest(fx, fy))  return JNI_TRUE;

    return JNI_FALSE;
}

static jstring JNICALL impl_nativeGetPendingCmd(JNIEnv* env, jclass clazz) {
    (void)clazz;
    int needed = yamgg_peekPendingCmdSize();
    if (needed <= 0) return nullptr;

    if (needed >= (INT_MAX - 16)) {
        LOGE("cmd size invalid: %d", needed);
        return nullptr;
    }

    std::vector<char> buf(needed + 16);
    int n = yamgg_popPendingCmd(buf.data(), (int)buf.size());
    if (n < 0) {
        buf.resize(-n);
        n = yamgg_popPendingCmd(buf.data(), (int)buf.size());
    }
    if (n <= 0) return nullptr;
    return env->NewStringUTF(buf.data());
}

static const JNINativeMethod g_methods[] = {
    {"nativeOnSurfaceCreated", "()V",  (void*)impl_nativeOnSurfaceCreated},
    {"nativeOnSurfaceChanged", "(II)V", (void*)impl_nativeOnSurfaceChanged},
    {"nativeOnDrawFrame",      "(II)V", (void*)impl_nativeOnDrawFrame},
    {"nativeOnTouch",          "(IFFI)V", (void*)impl_nativeOnTouch},
    {"nativeOnKey",            "(II)V", (void*)impl_nativeOnKey},
    {"nativeOnChar",           "(I)V",  (void*)impl_nativeOnChar},
    {"nativeOnScroll",         "(FF)V", (void*)impl_nativeOnScroll},
    {"nativeWantCaptureMouse", "()Z",   (void*)impl_nativeWantCaptureMouse},
    {"nativeWantTextInput",    "()Z",   (void*)impl_nativeWantTextInput},
    {"nativeHitTest",          "(FF)Z", (void*)impl_nativeHitTest},
    {"nativeGetPendingCmd",    "()Ljava/lang/String;", (void*)impl_nativeGetPendingCmd},
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

// ─── wrappers ───
void onSurfaceCreated(JNIEnv* env, jclass clazz) { impl_nativeOnSurfaceCreated(env, clazz); }
void onSurfaceChanged(JNIEnv* env, jclass clazz, jint w, jint h) { impl_nativeOnSurfaceChanged(env, clazz, w, h); }
void onDrawFrame(JNIEnv* env, jclass clazz, jint w, jint h) { impl_nativeOnDrawFrame(env, clazz, w, h); }
void onTouch(JNIEnv* env, jclass clazz, jint a, jfloat x, jfloat y, jint p) { impl_nativeOnTouch(env, clazz, a, x, y, p); }
void onKey(JNIEnv* env, jclass clazz, jint k, jint a) { impl_nativeOnKey(env, clazz, k, a); }
void onChar(JNIEnv* env, jclass clazz, jint c) { impl_nativeOnChar(env, clazz, c); }
void onScroll(JNIEnv* env, jclass clazz, jfloat dx, jfloat dy) { impl_nativeOnScroll(env, clazz, dx, dy); }
jboolean wantCaptureMouse(JNIEnv* env, jclass clazz) { return impl_nativeWantCaptureMouse(env, clazz); }
jboolean wantTextInput(JNIEnv* env, jclass clazz) { return impl_nativeWantTextInput(env, clazz); }
jboolean hitTest(JNIEnv* env, jclass clazz, jfloat x, jfloat y) { return impl_nativeHitTest(env, clazz, x, y); }

} // namespace jni
} // namespace yamgg
