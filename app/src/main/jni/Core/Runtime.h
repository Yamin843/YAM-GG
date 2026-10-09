#ifndef YAMGG_CORE_RUNTIME_H
#define YAMGG_CORE_RUNTIME_H

#include <jni.h>
#include <atomic>
#include <mutex>
#include <string>

namespace yamgg {

class Runtime {
public:
    static Runtime& instance();

    bool initialize(JavaVM* vm);
    void shutdown();
    bool isInitialized() const { return initialized_.load(); }

    JavaVM* vm() const { return vm_; }
    JNIEnv* env();
    bool attachCurrentThread(JNIEnv** env, bool* didAttach);
    void detachCurrentThread();

    std::string packageName() const { return package_name_; }
    std::string processName() const { return process_name_; }
    int apiLevel() const { return api_level_; }

    void setPackageName(const std::string& name) { package_name_ = name; }
    void setProcessName(const std::string& name) { process_name_ = name; }
    void setApiLevel(int level) { api_level_ = level; }

    std::string dexClassLoader() const { return dex_class_loader_; }
    void setDexClassLoader(const std::string& s) { dex_class_loader_ = s; }

    std::string modViewClassName() const { return modview_class_name_; }
    void setModViewClassName(const std::string& s) { modview_class_name_ = s; }

private:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    std::atomic<bool> initialized_{false};
    JavaVM* vm_{nullptr};
    std::string package_name_;
    std::string process_name_;
    std::string dex_class_loader_;
    std::string modview_class_name_{"com.yamgg.modview.ModView"};
    int api_level_{0};
    mutable std::mutex mu_;
};

#define YAMGG_RUNTIME yamgg::Runtime::instance()

} // namespace yamgg

#endif
