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
#include <thread>

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

// Forward declaration at namespace scope (extern "C" linkage specifications
// are only valid at namespace scope — not inside functions/lambdas).
extern "C" void yamgg_postCommand(const char* json);

// ===========================================================================
// init_thread
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

    jclass modViewClass = DexLoader::instance().findClass(env, "com.yamgg.modview.ModView");
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

    jclass helperClass = DexLoader::instance().findClass(env, "com.yamgg.modview.ModViewHelper");
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
        // All JS hooks are now embedded inside the bootstrap (see bootstrap_js.h).
        // They run automatically via setTimeout after cpp_ready.
    }

    // ─── Drive the JS poller from C++ (no setTimeout in QuickJS) ───
    // This thread pushes a yamgg_tick every 50ms, BUT only when the
    // command queue is empty. This prevents unbounded growth if JS
    // poller is dead — ticks naturally throttle to JS consumption rate.
    {
        std::thread ticker([]() {
            using namespace std::chrono;
            const auto interval = milliseconds(50);
            auto next = steady_clock::now();

            while (g_pump_running.load(std::memory_order_acquire)) {
                {
                    std::lock_guard<std::mutex> lk(g_cmdMutex);
                    // Only enqueue a tick if the queue is empty. If JS is
                    // backlogged, we skip — the JS will still process the
                    // real commands already pending.
                    if (g_cmdQueue.empty()) {
                        g_cmdQueue.push(
                            "{\"type\":\"yamgg_cmd\","
                            "\"payload\":{\"action\":\"yamgg_tick\","
                            "\"id\":0}}");
                    }
                }
                next += interval;
                std::this_thread::sleep_until(next);
            }
        });
        ticker.detach();
        LOGI("init_thread: tick driver started (50ms, throttled)");
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
// JNI_OnLoad
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

// ===========================================================================
// JNI_OnUnload
// ===========================================================================
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
// C++ → JS command queue (polled by JS via nativeGetPendingCmd)
// ===========================================================================
// Return semantics:
//   > 0 : bytes written (success)
//     0 : queue empty
//   < 0 : buffer too small — retry with abs(return) bytes
extern "C" int yamgg_popPendingCmd(char* out, int maxLen) {
    if (!out || maxLen <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_cmdMutex);
    if (g_cmdQueue.empty()) return 0;

    const std::string& cmd = g_cmdQueue.front();
    int n = static_cast<int>(cmd.size());

    if (n + 1 > maxLen) {
        // لا اقتطاع — أبلغ المُستدعي بالحجم المطلوب
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
    return (int)g_cmdQueue.front().size();
}

namespace {
std::atomic<std::uint64_t> g_droppedCmds{0};
std::atomic<std::int64_t>  g_lastDropLogMs{0};
}

extern "C" void yamgg_postCommand(const char* json) {
    if (!json) return;
    std::lock_guard<std::mutex> lk(g_cmdMutex);

    // لا cap. الطابور ينمو بحرية. إذا وصل إلى 10000 عنصر، نُسجّل
    // تحذيراً للمراقبة لكن لا نُسقط أي أمر. النظام الذي يتجاوز هذا
    // الحجم يعني أن JS poller لا يعمل — نحتاج معالجة ذلك في JS،
    // لا حذف بيانات المستخدم.
    if (g_cmdQueue.size() == 10000 || g_cmdQueue.size() == 100000) {
        int64_t now = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        int64_t last = g_lastDropLogMs.load();
        if (now - last > 5000) {
            g_lastDropLogMs.store(now);
            __android_log_print(ANDROID_LOG_WARN, "YAMGG",
                "cmd queue growing: size=%zu (JS poller slow?)",
                g_cmdQueue.size());
        }
    }
    g_cmdQueue.push(std::string(json));
}



// ===========================================================================
// JNI bridges for ModView native methods
// ===========================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnSurfaceCreated(JNIEnv* env, jclass clazz) {
    jni::onSurfaceCreated(env, clazz);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnSurfaceChanged(JNIEnv* env, jclass clazz,
                                                       jint w, jint h) {
    jni::onSurfaceChanged(env, clazz, w, h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnDrawFrame(JNIEnv* env, jclass clazz,
                                                  jint w, jint h) {
    jni::onDrawFrame(env, clazz, w, h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnTouch(JNIEnv* env, jclass clazz,
                                              jint action, jfloat x, jfloat y,
                                              jint pointerId) {
    jni::onTouch(env, clazz, action, x, y, pointerId);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnKey(JNIEnv* env, jclass clazz,
                                            jint keyCode, jint action) {
    jni::onKey(env, clazz, keyCode, action);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yamgg_modview_ModView_nativeOnScroll(JNIEnv* env, jclass clazz,
                                                jfloat dx, jfloat dy) {
    jni::onScroll(env, clazz, dx, dy);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yamgg_modview_ModView_nativeHitTest(JNIEnv* env, jclass clazz,
                                              jfloat x, jfloat y) {
    return jni::hitTest(env, clazz, x, y);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_yamgg_modview_ModView_nativeGetPendingCmd(JNIEnv*, jclass);



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
