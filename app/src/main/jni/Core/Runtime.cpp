#include "Runtime.h"

#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <sys/system_properties.h>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

Runtime::Runtime() = default;
Runtime::~Runtime() = default;

Runtime& Runtime::instance() {
    static Runtime inst;
    return inst;
}

static int readApiLevel() {
    char buf[PROP_VALUE_MAX] = {0};
    int len = __system_property_get("ro.build.version.sdk", buf);
    if (len <= 0) return 0;
    return atoi(buf);
}

static std::string readCmdline() {
    std::ifstream f("/proc/self/cmdline", std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && s.back() == '\0') s.pop_back();
    for (char& c : s) if (c == '\0') c = ':';
    return s;
}

static std::string readPackageName() {
    std::string cmdline = readCmdline();
    if (cmdline.empty()) return "";
    size_t colon = cmdline.find(':');
    if (colon == std::string::npos) return cmdline;
    return cmdline.substr(0, colon);
}

bool Runtime::initialize(JavaVM* vm) {
    std::lock_guard<std::mutex> lk(mu_);
    if (initialized_.load()) return true;
    if (!vm) {
        LOGE("Runtime::initialize: null VM");
        return false;
    }
    vm_ = vm;
    api_level_ = readApiLevel();
    package_name_ = readPackageName();
    process_name_ = readCmdline();
    LOGI("Runtime init: pkg=%s proc=%s api=%d",
         package_name_.c_str(), process_name_.c_str(), api_level_);
    initialized_.store(true);
    return true;
}

void Runtime::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;
    vm_ = nullptr;
    initialized_.store(false);
    LOGI("Runtime shutdown");
}

JNIEnv* Runtime::env() {
    if (!vm_) return nullptr;
    JNIEnv* e = nullptr;
    jint rc = vm_->GetEnv((void**)&e, JNI_VERSION_1_6);
    if (rc == JNI_OK) return e;
    return nullptr;
}

bool Runtime::attachCurrentThread(JNIEnv** env, bool* didAttach) {
    *didAttach = false;
    *env = nullptr;
    if (!vm_) return false;
    jint rc = vm_->GetEnv((void**)env, JNI_VERSION_1_6);
    if (rc == JNI_OK) return true;
    if (rc == JNI_EDETACHED) {
        JavaVMAttachArgs args{};
        args.version = JNI_VERSION_1_6;
        args.name = "YAMGG-Worker";
        args.group = nullptr;
        if (vm_->AttachCurrentThread(env, &args) == JNI_OK) {
            *didAttach = true;
            return true;
        }
    }
    return false;
}

void Runtime::detachCurrentThread() {
    if (vm_) vm_->DetachCurrentThread();
}

} // namespace yamgg
