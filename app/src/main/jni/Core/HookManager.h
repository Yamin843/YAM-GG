#ifndef YAMGG_CORE_HOOKMANAGER_H
#define YAMGG_CORE_HOOKMANAGER_H

#include <jni.h>
#include <string>

namespace yamgg {

class HookManager {
public:
    static HookManager& instance();

    bool installActivityHooks(JNIEnv* env);
    bool installApplicationHooks(JNIEnv* env);
    void uninstallAll(JNIEnv* env);

    bool isInstalled() const { return installed_; }

    void onActivityResumed(JNIEnv* env, jobject activity);
    void onActivityPaused(JNIEnv* env, jobject activity);
    void onActivityDestroyed(JNIEnv* env, jobject activity);

private:
    HookManager() = default;
    ~HookManager() = default;
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

    bool installed_{false};
    jclass activityClass_{nullptr};
    jmethodID onResumeMethod_{nullptr};
    jmethodID onPauseMethod_{nullptr};
    jmethodID onDestroyMethod_{nullptr};
};

#define YAMGG_HOOKS yamgg::HookManager::instance()

} // namespace yamgg

#endif
