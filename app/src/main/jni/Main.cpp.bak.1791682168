// ===========================================================================
// Main.cpp — YAM-GG entry point
// ===========================================================================
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <queue>
#include <cstring>
#include <cstdint>
#include <thread>
#include <mutex>

#include "Core/Runtime.h"
#include "Core/DexLoader.h"
#include "Bridge/YamBridge.h"
#include "UI/Renderer.h"
#include "JNI/NativeMethods.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace yamgg;

extern "C" void yamgg_pump_once();

static JavaVM* g_vm = nullptr;
static jclass  g_modViewClass = nullptr;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_pump_running{true};
static std::mutex         g_cmdMutex;
static std::queue<std::string> g_cmdQueue;

// Forward declarations at namespace scope (linkage specifiers are NOT
// valid inside function bodies / lambdas).
extern "C" int  yamgg_popPendingCmd(char* out, int maxLen);
extern "C" int  yamgg_peekPendingCmdSize(void);
extern "C" void yamgg_postCommand(const char* json);

// ===========================================================================
// Command queue (C++ → JS bridge)
// ===========================================================================
extern "C" int yamgg_popPendingCmd(char* out, int maxLen) {
    if (!out || maxLen <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_cmdMutex);
    if (g_cmdQueue.empty()) return 0;

    const std::string& cmd = g_cmdQueue.front();
    int n = static_cast<int>(cmd.size());
    if (n + 1 > maxLen) {
        // Buffer too small — report the required size without popping.
        return -(n + 1);
    }
    std::memcpy(out, cmd.data(), n);
    out[n] = 0;
    g_cmdQueue.pop();
    return n;
}

extern "C" int yamgg_peekPendingCmdSize(void) {
    std::lock_guard<std::mutex> lk(g_cmdMutex);
    if (g_cmdQueue.empty()) return 0;
    return static_cast<int>(g_cmdQueue.front().size());
}

extern "C" void yamgg_postCommand(const char* json) {
    if (!json) return;
    std::lock_guard<std::mutex> lk(g_cmdMutex);
    g_cmdQueue.push(std::string(json));
}

// ===========================================================================
// init_thread — runs on a detached pthread, has its own JNIEnv
// ===========================================================================
static void* init_thread(void*) {
    LOGI("init_thread: begin");

    JNIEnv* env = nullptr;
    bool attached = false;
    if (!Runtime::instance().attachCurrentThread(&env, &attached)) {
        LOGE("init_thread: cannot attach to JVM");
        return nullptr;
    }

    if (!DexLoader::instance().loadEmbeddedDex(env)) {
        LOGE("init_thread: failed to load embedded dex");
        if (attached) Runtime::instance().detachCurrentThread();
        return nullptr;
    }

    jclass modViewClass = DexLoader::instance().findClass(
        env, "com.yamgg.modview.ModView");
    if (!modViewClass) {
        LOGE("init_thread: ModView class not found");
        if (attached) Runtime::instance().detachCurrentThread();
        return nullptr;
    }

    g_modViewClass = reinterpret_cast<jclass>(env->NewGlobalRef(modViewClass));
    env->DeleteLocalRef(modViewClass);

    if (!jni::registerNativeMethods(env, g_modViewClass)) {
        LOGE("init_thread: RegisterNatives failed");
    } else {
        LOGI("init_thread: native methods registered");
    }

    jclass helperClass = DexLoader::instance().findClass(
        env, "com.yamgg.modview.ModViewHelper");
    if (helperClass) {
        jmethodID installMethod = env->GetStaticMethodID(
            helperClass, "install", "(Ljava/lang/ClassLoader;)V");
        if (installMethod) {
            jobject cl = DexLoader::instance().classLoader(env);
            env->CallStaticVoidMethod(helperClass, installMethod, cl);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteLocalRef(helperClass);
    }

    if (!YamBridge::instance().initialize()) {
        LOGE("init_thread: YamBridge init failed");
    } else {
        LOGI("init_thread: YamBridge ready");
    }

    LOGI("init_thread: complete — entering GMainContext pump loop");
    {
        LOGI("init_thread: pump loop starting");
        while (g_pump_running.load(std::memory_order_acquire)) {
            yamgg_pump_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        LOGI("init_thread: pump loop exited");
    }

    if (attached) Runtime::instance().detachCurrentThread();
    return nullptr;
}

// ===========================================================================
// JNI_OnLoad / JNI_OnUnload
// ===========================================================================
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    LOGI("JNI_OnLoad called");
    g_vm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("JNI_OnLoad: GetEnv failed");
        return JNI_ERR;
    }

    if (!Runtime::instance().initialize(vm)) {
        LOGE("JNI_OnLoad: Runtime init failed");
        return JNI_ERR;
    }

    if (g_initialized.exchange(true)) {
        LOGI("JNI_OnLoad: already initialized");
        return JNI_VERSION_1_6;
    }

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, init_thread, nullptr) != 0) {
        LOGE("JNI_OnLoad: pthread_create failed");
        pthread_attr_destroy(&attr);
        return JNI_ERR;
    }
    pthread_attr_destroy(&attr);

    LOGI("JNI_OnLoad: init thread launched");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    LOGI("JNI_OnUnload called");

    g_pump_running.store(false, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    JNIEnv* env = nullptr;
    if (g_vm) g_vm->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (env) {
        DexLoader::instance().detach(env);
        if (g_modViewClass) {
            env->DeleteGlobalRef(g_modViewClass);
            g_modViewClass = nullptr;
        }
    }

    YamBridge::instance().shutdown();
    Renderer::instance().shutdown();
    Runtime::instance().shutdown();
}

// ===========================================================================
// JNI bridges for ModView native methods
// ===========================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnSurfaceCreated(JNIEnv* env, jclass clazz) {
    jni::onSurfaceCreated(env, clazz);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnSurfaceChanged(
    JNIEnv* env, jclass clazz, jint w, jint h) {
    jni::onSurfaceChanged(env, clazz, w, h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnDrawFrame(
    JNIEnv* env, jclass clazz, jint w, jint h) {
    jni::onDrawFrame(env, clazz, w, h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnTouch(
    JNIEnv* env, jclass clazz, jint action, jfloat x, jfloat y, jint pointerId) {
    jni::onTouch(env, clazz, action, x, y, pointerId);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnKey(
    JNIEnv* env, jclass clazz, jint keyCode, jint action) {
    jni::onKey(env, clazz, keyCode, action);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnChar(
    JNIEnv* env, jclass clazz, jint codepoint) {
    jni::onChar(env, clazz, codepoint);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnScroll(
    JNIEnv* env, jclass clazz, jfloat dx, jfloat dy) {
    jni::onScroll(env, clazz, dx, dy);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yamgg_modview_ModView_nativeWantCaptureMouse(
    JNIEnv* env, jclass clazz) {
    return jni::wantCaptureMouse(env, clazz);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yamgg_modview_ModView_nativeWantTextInput(
    JNIEnv* env, jclass clazz) {
    return jni::wantTextInput(env, clazz);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yamgg_modview_ModView_nativeHitTest(
    JNIEnv* env, jclass clazz, jfloat x, jfloat y) {
    return jni::hitTest(env, clazz, x, y);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_yamgg_modview_ModView_nativeGetPendingCmd(
    JNIEnv* env, jclass clazz) {
    (void)clazz;
    int needed = yamgg_peekPendingCmdSize();
    if (needed <= 0) return nullptr;

    std::vector<char> buf(needed + 16);
    int n = yamgg_popPendingCmd(buf.data(),
                                static_cast<int>(buf.size()));
    if (n < 0) {
        buf.resize(-n);
        n = yamgg_popPendingCmd(buf.data(),
                                static_cast<int>(buf.size()));
    }
    if (n <= 0) return nullptr;
    return env->NewStringUTF(buf.data());
}

// ===========================================================================
// Constructor / Destructor
// ===========================================================================
__attribute__((constructor))
static void lib_constructor() {
    LOGI("YAM-GG library loaded");
}

__attribute__((destructor))
static void lib_destructor() {
    LOGI("YAM-GG library unloaded");
}
