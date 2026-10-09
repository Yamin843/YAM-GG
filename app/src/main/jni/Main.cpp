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
#include <thread>

// ── GLib forward declarations (avoid pulling glib.h into app source) ──
extern "C" {
    typedef struct _GMainContext GMainContext;
    typedef int gboolean;
    GMainContext* g_main_context_get_thread_default(void);
    GMainContext* g_main_context_default(void);
    gboolean      g_main_context_iteration(GMainContext* context, gboolean may_block);
}
#ifndef FALSE
#  define FALSE 0
#endif

#include "Core/Runtime.h"
#include "Core/DexLoader.h"
#include "Bridge/YamBridge.h"
#include "UI/Renderer.h"
#include "JNI/NativeMethods.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* g_vm = nullptr;
static jclass g_modViewClass = nullptr;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_pump_running{true};

using namespace yamgg;

// ===========================================================================
// Activity.onResume hook + current-activity attach
// ===========================================================================
static const char* kActivityHookJs = R"JS(
(function () {
    "use strict";
    if (typeof Java === "undefined") {
        send({type:"hook_error", message:"Java undefined"});
        return;
    }

    // ── STEP 1 — attach to CURRENT activity (no onResume needed) ──
    try {
        Java.performNow(function () {
            try {
                Java.scheduleOnMainThread(function () {
                    try {
                        var ActivityThread = Java.use("android.app.ActivityThread");
                        var at = ActivityThread.currentActivityThread();
                        if (!at) { send({type:"attach_now_info", message:"no ActivityThread"}); return; }

                        var mActivities = at.mActivities.value;
                        if (!mActivities) { send({type:"attach_now_info", message:"no mActivities"}); return; }

                        var n = mActivities.size();
                        for (var i = 0; i < n; i++) {
                            try {
                                var rec = mActivities.valueAt(i);
                                if (!rec) continue;
                                var act = null;
                                try { act = rec.activity.value; } catch (e) { continue; }
                                if (!act) continue;
                                try { if (act.isFinishing()) continue; } catch (e) {}

                                var ModView = Java.use("com.yamgg.modview.ModView");
                                ModView.attach(act);
                                send({type:"attach_now_ok",
                                      className: "" + act.getClass().getName(),
                                      index: i});
                                return;
                            } catch (e) {
                                send({type:"attach_now_skip", index: i, message: "" + e});
                            }
                        }
                        send({type:"attach_now_none"});
                    } catch (e) {
                        send({type:"attach_now_error", message: "" + e});
                    }
                });
            } catch (e) {
                send({type:"attach_now_outer", message: "" + e});
            }
        });
    } catch (e) {
        send({type:"attach_now_outer2", message: "" + e});
    }

    // ── STEP 2 — hook onResume for future activities ──
    try {
        Java.performNow(function () {
            try {
                var Activity     = Java.use("android.app.Activity");
                var origOnResume = Activity.onResume;
                Activity.onResume.implementation = function () {
                    try { origOnResume.call(this); } catch (e) {}
                    try {
                        var ModView = Java.use("com.yamgg.modview.ModView");
                        ModView.attach(this);
                        send({type:"modview_attached",
                              className: "" + this.getClass().getName()});
                    } catch (e) {
                        send({type:"attach_error", message: "" + e});
                    }
                };
                send({type:"hook_installed"});
            } catch (e) {
                send({type:"hook_error", message: "install: " + e});
            }
        });
    } catch (e) {
        send({type:"hook_outer_error", message: "" + e});
    }
})();
)JS";

// ===========================================================================
// AppsFlyer trackEvent hook
// ===========================================================================
static const char* kAppsFlyerHookJs = R"JS(
(function () {
    "use strict";
    if (typeof Java === "undefined") {
        send({type:"appsflyer_error", message:"Java undefined"});
        return;
    }

    var LOG_PATH = "/storage/emulated/0/Download/appsflyer_calls.log";

    function writeLog(line) {
        try {
            var FileWriter = Java.use("java.io.FileWriter");
            var fw = FileWriter.$new(LOG_PATH, true);
            try { fw.write(line + "\n"); fw.flush(); }
            finally { fw.close(); }
        } catch (e) {
            send({type:"appsflyer_log_error", message:"" + e});
        }
    }

    Java.performNow(function () {
        try {
            var C = Java.use("com.appsflyer.unity.AppsFlyerAndroidWrapper");
            send({type:"appsflyer_class_ok"});

            // Overload 1: (String, HashMap) -> void
            try {
                var m2 = C.trackEvent.overload("java.lang.String", "java.util.HashMap");
                m2.implementation = function (name, params) {
                    writeLog("" + new Date() + "  trackEvent/2  name=" + name + "  params=" + params);
                    send({type:"appsflyer_hit", overload:"2", name:"" + name});
                    return m2.call(this, name, params);
                };
                send({type:"appsflyer_hook_ok", overload:"2"});
            } catch (e) {
                send({type:"appsflyer_hook_err", overload:"2", message:"" + e});
            }

            // Overload 2: (String, HashMap, boolean, String) -> void
            try {
                var m4 = C.trackEvent.overload(
                    "java.lang.String", "java.util.HashMap",
                    "boolean", "java.lang.String");
                m4.implementation = function (name, params, isRevenue, currency) {
                    writeLog("" + new Date() + "  trackEvent/4  name=" + name
                             + "  params=" + params + "  isRevenue=" + isRevenue
                             + "  currency=" + currency);
                    send({type:"appsflyer_hit", overload:"4", name:"" + name});
                    return m4.call(this, name, params, isRevenue, currency);
                };
                send({type:"appsflyer_hook_ok", overload:"4"});
            } catch (e) {
                send({type:"appsflyer_hook_err", overload:"4", message:"" + e});
            }

            send({type:"appsflyer_ready"});
        } catch (e) {
            send({type:"appsflyer_class_error", message:"" + e});
        }
    });
})();
)JS";

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
        LOGE("init_thread: ModView class not found in dex");
        if (attached) Runtime::instance().detachCurrentThread();
        return nullptr;
    }

    g_modViewClass = reinterpret_cast<jclass>(env->NewGlobalRef(modViewClass));
    env->DeleteLocalRef(modViewClass);

    if (!jni::registerNativeMethods(env, g_modViewClass)) {
        LOGE("init_thread: failed to register native methods");
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

        auto r1 = YamBridge::instance().loadScript("__activity_hook__", kActivityHookJs);
        if (r1.ok) LOGI("init_thread: Activity hook script OK");
        else        LOGE("init_thread: Activity hook script FAILED: %s", r1.error.c_str());

        auto r2 = YamBridge::instance().loadScript("__appsflyer_hook__", kAppsFlyerHookJs);
        if (r2.ok) LOGI("init_thread: AppsFlyer hook script OK");
        else        LOGE("init_thread: AppsFlyer hook script FAILED: %s", r2.error.c_str());
    }

    LOGI("init_thread: complete — entering GMainContext pump loop");

    // ── Pump loop — keeps JS scheduler alive ──
    {
        GMainContext* ctx = g_main_context_get_thread_default();
        if (!ctx) ctx = g_main_context_default();
        LOGI("init_thread: pump loop starting (ctx=%p)", (void*)ctx);

        while (g_pump_running.load(std::memory_order_acquire)) {
            int processed = 0;
            while (g_main_context_iteration(ctx, FALSE)) { ++processed; }
            (void)processed;
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
