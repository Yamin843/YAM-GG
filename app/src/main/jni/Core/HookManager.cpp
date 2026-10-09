#include "HookManager.h"
#include "DexLoader.h"
#include "Runtime.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

HookManager& HookManager::instance() {
    static HookManager inst;
    return inst;
}

bool HookManager::installActivityHooks(JNIEnv* env) {
    if (!env) return false;
    if (installed_) return true;

    jclass activityClass = env->FindClass("android/app/Activity");
    if (!activityClass) {
        env->ExceptionClear();
        LOGE("Activity class not found");
        return false;
    }
    activityClass_ = reinterpret_cast<jclass>(env->NewGlobalRef(activityClass));
    env->DeleteLocalRef(activityClass);

    onResumeMethod_ = env->GetMethodID(activityClass_, "onResume", "()V");
    onPauseMethod_ = env->GetMethodID(activityClass_, "onPause", "()V");
    onDestroyMethod_ = env->GetMethodID(activityClass_, "onDestroy", "()V");

    if (!onResumeMethod_ || !onPauseMethod_ || !onDestroyMethod_) {
        env->ExceptionClear();
        LOGE("Activity lifecycle methods not found");
        return false;
    }

    LOGI("Activity hooks prepared: %p %p %p",
         onResumeMethod_, onPauseMethod_, onDestroyMethod_);

    installed_ = true;
    return true;
}

bool HookManager::installApplicationHooks(JNIEnv* env) {
    if (!env) return false;
    jclass appClass = env->FindClass("android/app/Application");
    if (!appClass) {
        env->ExceptionClear();
        return false;
    }
    env->DeleteLocalRef(appClass);
    return true;
}

void HookManager::uninstallAll(JNIEnv* env) {
    if (activityClass_) {
        env->DeleteGlobalRef(activityClass_);
        activityClass_ = nullptr;
    }
    installed_ = false;
}

void HookManager::onActivityResumed(JNIEnv* env, jobject activity) {
    if (!env || !activity) return;
    jclass helperClass = YAMGG_DEXLOADER.findClass(env, "com.yamgg.modview.ModViewHelper");
    if (!helperClass) {
        env->ExceptionClear();
        LOGE("ModViewHelper class not found");
        return;
    }
    jmethodID m = env->GetStaticMethodID(
            helperClass, "onActivityResumed", "(Landroid/app/Activity;)V");
    if (m) {
        env->CallStaticVoidMethod(helperClass, m, activity);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            LOGE("onActivityResumed call failed");
        }
    } else {
        env->ExceptionClear();
        LOGE("onActivityResumed method not found");
    }
    env->DeleteLocalRef(helperClass);
}

void HookManager::onActivityPaused(JNIEnv* env, jobject activity) {
    if (!env || !activity) return;
    jclass helperClass = YAMGG_DEXLOADER.findClass(env, "com.yamgg.modview.ModViewHelper");
    if (!helperClass) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(
            helperClass, "onActivityPaused", "(Landroid/app/Activity;)V");
    if (m) {
        env->CallStaticVoidMethod(helperClass, m, activity);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(helperClass);
}

void HookManager::onActivityDestroyed(JNIEnv* env, jobject activity) {
    if (!env || !activity) return;
    jclass helperClass = YAMGG_DEXLOADER.findClass(env, "com.yamgg.modview.ModViewHelper");
    if (!helperClass) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(
            helperClass, "onActivityDestroyed", "(Landroid/app/Activity;)V");
    if (m) {
        env->CallStaticVoidMethod(helperClass, m, activity);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(helperClass);
}

} // namespace yamgg
