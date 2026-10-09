#include "NativeMethods.h"
#include "../Core/Runtime.h"
#include "../UI/Renderer.h"
#include "../UI/MainWindow.h"
#include "../UI/Tabs/FileBrowser.h"

#include <android/log.h>
#include <vector>

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


static jboolean JNICALL impl_nativeHitTest(JNIEnv* env, jclass clazz,
                                             jfloat x, jfloat y) {
    (void)env; (void)clazz;
    float fx = (float)x;
    float fy = (float)y;

    // Priority: FileBrowser (on top) → MainWindow (or minimized chip).
    // Every top-level window registers its rect during draw().
    if (FileBrowser::instance().hitTest(fx, fy)) return JNI_TRUE;
    if (MainWindow::instance().hitTest(fx, fy)) return JNI_TRUE;

    return JNI_FALSE;
}

static jboolean JNICALL impl_nativeWantCaptureMouse(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return Renderer::instance().wantCaptureMouse() ? JNI_TRUE : JNI_FALSE;
}

static void JNICALL impl_nativeOnKey(JNIEnv* env, jclass clazz,
                                        jint keyCode, jint action) {
    (void)env; (void)clazz;
    Renderer::instance().onKey((int)keyCode, (int)action);
}

static void JNICALL impl_nativeOnChar(JNIEnv* env, jclass clazz, jint codepoint) {
    (void)env; (void)clazz;
    Renderer::instance().onChar((unsigned int)codepoint);
}

static void JNICALL impl_nativeOnScroll(JNIEnv* env, jclass clazz, jfloat dx, jfloat dy) {
    (void)env; (void)clazz;
    Renderer::instance().onScroll((float)dx, (float)dy);
}

static jboolean JNICALL impl_nativeWantTextInput(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return Renderer::instance().wantTextInput() ? JNI_TRUE : JNI_FALSE;
}

extern "C" int yamgg_popPendingCmd(char* out, int maxLen);

extern "C" int yamgg_popPendingCmd(char* out, int maxLen);
extern "C" int yamgg_peekPendingCmdSize(void);

static jstring JNICALL impl_nativeGetPendingCmd(JNIEnv* env, jclass clazz) {
    (void)clazz;

    int needed = yamgg_peekPendingCmdSize();
    if (needed <= 0) return nullptr;

    // تخصيص دقيق — لا سقف. إذا الأمر 500 MB، نخصّص 500 MB.
    // الحماية الوحيدة: reject سالب (خطأ protocol)، و reject ما يتجاوز
    // حد jstring (INT_MAX - 16) لأن JNI NewStringUTF يقتطع عنده بصمت.
    if (needed <= 0 || needed >= (INT_MAX - 16)) {
        __android_log_print(ANDROID_LOG_ERROR, "YAMGG",
            "cmd size invalid: %d", needed);
        return nullptr;
    }

    std::vector<char> buf(needed + 16);
    int n = yamgg_popPendingCmd(buf.data(), static_cast<int>(buf.size()));

    // Buffer too small — retry with exact size (نظرياً لا يحدث، لكن احتياط)
    if (n < 0) {
        buf.resize(-n);
        n = yamgg_popPendingCmd(buf.data(), static_cast<int>(buf.size()));
    }
    if (n <= 0) return nullptr;

    return env->NewStringUTF(buf.data());
}

static const JNINativeMethod g_methods[] = {
    {"nativeOnSurfaceCreated", "()V", (void*)impl_nativeOnSurfaceCreated},
    {"nativeOnSurfaceChanged", "(II)V", (void*)impl_nativeOnSurfaceChanged},
    {"nativeOnDrawFrame", "(II)V", (void*)impl_nativeOnDrawFrame},
    {"nativeOnTouch", "(IFFI)V", (void*)impl_nativeOnTouch},
    {"nativeOnKey", "(II)V", (void*)impl_nativeOnKey},
    {"nativeGetPendingCmd", "()Ljava/lang/String;", (void*)impl_nativeGetPendingCmd},
    {"nativeWantCaptureMouse", "()Z", (void*)impl_nativeWantCaptureMouse},
    {"nativeHitTest", "(FF)Z", (void*)impl_nativeHitTest},
    {"nativeOnChar", "(I)V", (void*)impl_nativeOnChar},
    {"nativeWantTextInput", "()Z", (void*)impl_nativeWantTextInput},
    {"nativeOnScroll", "(FF)V", (void*)impl_nativeOnScroll},
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
void onKey(JNIEnv* env, jclass clazz, jint k, jint a) { (void)env; (void)clazz; (void)k; (void)a; }
jboolean wantCaptureMouse(JNIEnv* env, jclass clazz) { return impl_nativeWantCaptureMouse(env, clazz); }
jboolean hitTest(JNIEnv* env, jclass clazz, jfloat x, jfloat y) { return impl_nativeHitTest(env, clazz, x, y); }
void onChar(JNIEnv* env, jclass clazz, jint c) { impl_nativeOnChar(env, clazz, c); }
jboolean wantTextInput(JNIEnv* env, jclass clazz) { return impl_nativeWantTextInput(env, clazz); }
void onScroll(JNIEnv* env, jclass clazz, jfloat dx, jfloat dy) { impl_nativeOnScroll(env, clazz, dx, dy); }

} // namespace jni
} // namespace yamgg
