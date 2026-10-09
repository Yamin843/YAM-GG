#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <dlfcn.h>

#include "Core/Runtime.h"
#include "Core/DexLoader.h"
#include "Core/HookManager.h"
#include "Bridge/YamBridge.h"
#include "UI/Renderer.h"
#include "JNI/NativeMethods.h"
#include "yam.hpp"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* g_vm = nullptr;
static jclass g_modViewClass = nullptr;
static std::atomic<bool> g_initialized{false};

using namespace yamgg;

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
        HookManager::instance().installActivityHooks(env);

        // ─────────────────────────────────────────────────────────
        //  تثبيت hook فعلي على Activity.onResume
        //  (HookManager يحضّر method IDs فقط؛ لا يثبّت hook)
        //  نستخدم YAM Java bridge + frida-java-bridge عبر JS
        // ─────────────────────────────────────────────────────────
        LOGI("init_thread: installing Activity.onResume hook via JS");

        static const char* kActivityHookJs = R"JS(
(function() {
    "use strict";

    if (typeof Java === "undefined") {
        send({type:"hook_error", message:"Java undefined"});
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // STEP 1 — Force class loader (unblocks Java.perform queue)
    // ═══════════════════════════════════════════════════════════
    try {
        Java.performNow(function() {
            try {
                if (Java.classFactory.loader === null) {
                    var AT = Java.use("android.app.ActivityThread");
                    var app = AT.currentApplication();
                    if (app !== null) {
                        Java.classFactory.loader = app.getClassLoader();
                        send({type:"hook_info", message:"loader set"});
                    }
                }
            } catch(e) {
                send({type:"hook_info", message:"loader set failed: " + e});
            }
        });
    } catch(e) {
        send({type:"hook_error", message:"performNow(loader): " + e});
    }

    // ═══════════════════════════════════════════════════════════
    // STEP 2 — Attach to CURRENT activity immediately
    // ═══════════════════════════════════════════════════════════
    Java.performNow(function() {
        try {
            var attached = false;
            var ActivityThread = Java.use("android.app.ActivityThread");
            var at = ActivityThread.currentActivityThread();

            if (at !== null) {
                var records = at.mActivities.value;
                var n = records.size();

                for (var i = 0; i < n; i++) {
                    var rec = records.valueAt(i);
                    if (!rec) continue;

                    var act = null;
                    try { act = rec.activity.value; } catch(e) {}
                    if (act === null || act === undefined) continue;

                    try {
                        var isFinishing = act.isFinishing();
                        if (isFinishing) continue;
                    } catch(e) {}

                    try {
                        var ModView = Java.use("com.yamgg.modview.ModView");
                        ModView.attach(act);
                        send({
                            type: "modview_attached_now",
                            className: "" + act.getClass().getName(),
                            index: i
                        });
                        attached = true;
                        break;
                    } catch(e) {
                        send({type:"attach_now_error", message:"" + e});
                    }
                }
            }

            if (!attached) {
                send({type:"hook_info", message:"no active activity yet"});
            }
        } catch(e) {
            send({type:"hook_error", message:"current-activity scan: " + e});
        }
    });

    // ═══════════════════════════════════════════════════════════
    // STEP 3 — Hook onResume for any FUTURE activity
    // ═══════════════════════════════════════════════════════════
    Java.performNow(function() {
        try {
            var Activity = Java.use("android.app.Activity");
            var origOnResume = Activity.onResume;

            Activity.onResume.implementation = function() {
                origOnResume.call(this);

                try {
                    var ModView = Java.use("com.yamgg.modview.ModView");
                    ModView.attach(this);
                    send({
                        type: "modview_attached",
                        className: "" + this.getClass().getName()
                    });
                } catch(e) {
                    send({type:"attach_error", message:"" + e});
                }
            };

            send({type:"activity_hook_installed"});
        } catch(e) {
            send({type:"hook_error", message:"hook install: " + e});
        }
    });
})();
)JS";

        auto r = YamBridge::instance().loadScript("__activity_hook__", kActivityHookJs);
        if (r.ok) {
            LOGI("Activity hook script loaded OK");
        } else {
            LOGE("Activity hook script failed: %s", r.error.c_str());
        }

        // 3) مستمعي الأحداث للتشخيص
        static bool router_installed = false;
        if (!router_installed) {
            router_installed = true;
            yam::events::on("hook_error", [](const yam::Event& ev) {
                LOGE("HOOK-ERROR: %s", ev.get_str("message").c_str());
            });
            yam::events::on("hook_info", [](const yam::Event& ev) {
                LOGI("HOOK-INFO: %s", ev.get_str("message").c_str());
            });
            yam::events::on("activity_hook_installed", [](const yam::Event&) {
                LOGI("Activity.onResume hook INSTALLED");
            });
            yam::events::on("modview_attached", [](const yam::Event& ev) {
                LOGI("ModView attached to %s", ev.get_str("className").c_str());
            });
            yam::events::on("attach_error", [](const yam::Event& ev) {
                LOGE("ModView attach error: %s", ev.get_str("message").c_str());
            });
        }
    }

    LOGI("init_thread: complete");

    if (attached) Runtime::instance().detachCurrentThread();
    return nullptr;
}

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

    JNIEnv* env = nullptr;
    if (g_vm) g_vm->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (env) {
        HookManager::instance().uninstallAll(env);
        if (g_modViewClass) {
            env->DeleteGlobalRef(g_modViewClass);
            g_modViewClass = nullptr;
        }
    }

    YamBridge::instance().shutdown();
    Renderer::instance().shutdown();
    Runtime::instance().shutdown();
}

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

__attribute__((constructor))
static void lib_constructor() {
    LOGI("YAM-GG library loaded");
}

__attribute__((destructor))
static void lib_destructor() {
    LOGI("YAM-GG library unloaded");
}
