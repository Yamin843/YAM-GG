#include "YamBridge.h"
#include "../Core/Runtime.h"
#include "../UI/Tabs/JSConsole.h"

#include "yam.hpp"

#include <android/log.h>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <mutex>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {
std::mutex                                              g_scriptLoadMutex;
std::condition_variable                                 g_scriptLoadCv;
std::unordered_map<std::string, std::pair<bool,bool>>   g_scriptLoadResult;
std::unordered_map<std::string, std::string>            g_scriptLoadError;
}

YamBridge::YamBridge() = default;
YamBridge::~YamBridge() = default;

YamBridge& YamBridge::instance() {
    static YamBridge inst;
    return inst;
}

bool YamBridge::initialize() {
    std::lock_guard<std::mutex> lk(mu_);
    if (initialized_.load()) return true;
    if (!Runtime::instance().isInitialized()) return false;

    yam::EntryOptions opts;
    opts.log_level       = yam::LogLevel::Info;
    opts.enable_java     = true;
    opts.enable_console  = false;
    opts.enable_debugger = false;
    opts.worker_threads  = 0;
    opts.script_name     = "yamgg_bridge";
    opts.backend         = yam::BackendKind::QJS;

    auto r = yam::YAM::init(opts);
    if (!r) {
        LOGE("YAM::init failed: %s", r.error_message().c_str());
        return false;
    }

    auto& bridge = yam::JavaScriptBridge::instance();
    if (!bridge.is_ready()) {
        LOGE("bridge not ready after YAM::init");
        return false;
    }

    bridge.set_console_callback([this](const yam::String& l, const yam::String& s) {
        onConsole(l, s);
    });

    bridge.set_eval_callback([this](yam::u64 id, bool ok,
                                     const yam::String& res,
                                     const yam::String& err) {
        onEvalResult(id, ok, res, err);
    });

    bridge.set_hook_callback([this](
        yam::i64 cbId, const yam::String& phase,
        const std::vector<yam::u64>& args,
        yam::u64 thisH, yam::u64 retH,
        bool isVoid, const yam::String& exMsg)
    {
        if (hookCb_) {
            try { hookCb_(cbId, phase, args, thisH, retH, isVoid, exMsg); }
            catch (const std::exception& e) { LOGE("hook cb: %s", e.what()); }
        }
    });

    installEventRouter();

    yam::events::on("user_script_loaded", [](const yam::Event& ev) {
        std::string nm = ev.get_str("name");
        std::lock_guard<std::mutex> lk(g_scriptLoadMutex);
        g_scriptLoadResult[nm] = {true, ev.get_bool("ok", false)};
        g_scriptLoadError[nm]  = ev.get_str("error");
        g_scriptLoadCv.notify_all();
    });
    yam::events::on("script_failed", [](const yam::Event& ev) {
        std::string nm = ev.get_str("name");
        std::lock_guard<std::mutex> lk(g_scriptLoadMutex);
        g_scriptLoadResult[nm] = {true, false};
        g_scriptLoadError[nm]  = ev.get_str("error");
        g_scriptLoadCv.notify_all();
    });

    ready_.store(true);
    initialized_.store(true);
    LOGI("YamBridge: ready");

    // Hook watchdog (60s)
    std::thread watchdog([]() {
        using namespace std::chrono;
        for (;;) {
            std::this_thread::sleep_for(seconds(60));
            if (!YamBridge::instance().isReady()) break;
            try {
                auto n = yam::JavaHookManager::instance().size();
                if (n >= 256) {
                    __android_log_print(ANDROID_LOG_WARN, "YAMGG",
                        "hook watchdog: %zu java hooks active", n);
                }
            } catch (...) {}
        }
    });
    watchdog.detach();
    LOGI("YamBridge: hook watchdog started (60s)");

    return true;
}

void YamBridge::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;

    try {
        auto& bridge = yam::JavaScriptBridge::instance();
        bridge.set_hook_callback(nullptr);
        bridge.set_console_callback(nullptr);
        bridge.set_eval_callback(nullptr);
    } catch (...) {}

    hookCb_ = nullptr;

    try {
        yam::JavaHookManager::instance().clear();
    } catch (...) {}

    try { yam::YAM::shutdown(); } catch (...) {}

    {
        std::lock_guard<std::mutex> lk2(g_scriptLoadMutex);
        g_scriptLoadResult.clear();
        g_scriptLoadError.clear();
    }
    scriptNames_.clear();

    ready_.store(false);
    initialized_.store(false);
}

void YamBridge::installEventRouter() {
    static std::once_flag once_;
    std::call_once(once_, []() {
        yam::events::on("log", [](const yam::Event& ev) {
            std::string lvl = ev.get_str("level", "info");
            std::string msg = ev.get_str("message");
            if (lvl == "error") LOGE("[js] %s", msg.c_str());
            else if (lvl == "warn") __android_log_print(ANDROID_LOG_WARN, "YAMGG", "[js] %s", msg.c_str());
            else LOGI("[js] %s", msg.c_str());
        });
        yam::events::on("error", [](const yam::Event& ev) {
            LOGE("[js] %s", ev.get_str("message").c_str());
        });
        yam::events::on("agent_ready", [](const yam::Event&) {
            LOGI("agent_ready from embedded bridge");
        });
    });
}

void YamBridge::onEvalResult(unsigned long long id, bool ok,
                              const std::string& result,
                              const std::string& error) {
    (void)id;
    try {
        if (ok) {
            if (!result.empty()) JSConsole::instance().pushOutput(result);
        } else if (!error.empty()) {
            JSConsole::instance().pushOutput("[error] " + error);
        }
    } catch (const std::exception& e) { LOGE("onEvalResult: %s", e.what()); }
    catch (...) { LOGE("onEvalResult: unknown"); }
}

void YamBridge::onConsole(const std::string& level, const std::string& line) {
    std::string prefix;
    if (level == "error")      prefix = "[error] ";
    else if (level == "warn")  prefix = "[warn] ";
    else                       prefix = "";

    try { JSConsole::instance().pushOutput(prefix + line); } catch (...) {}

    if (level == "error") {
        if (errorSink_) { try { errorSink_(line); } catch (...) {} }
        LOGE("[js] %s", line.c_str());
    } else {
        if (outputSink_) { try { outputSink_(line); } catch (...) {} }
        LOGI("[js] %s", line.c_str());
    }
}

YamBridge::EvalResult YamBridge::evaluate(const std::string& code) {
    EvalResult out;
    if (!ready_.load()) { out.error = "bridge not ready"; return out; }

    try {
        auto r = yam::JavaScriptBridge::instance().eval(code);
        if (!r) { out.error = r.error_message(); return out; }
        const auto& rep = r.value();
        if (rep.kind == "value" || rep.kind == "pong") {
            out.ok = true; out.output = rep.result;
        } else if (rep.kind == "null") {
            out.ok = true; out.output = "null";
        } else if (rep.kind == "void") {
            out.ok = true;
        } else if (rep.kind == "error" || !rep.error.empty()) {
            out.ok = false;
            out.error = rep.error.empty() ? "unknown eval error" : rep.error;
        } else {
            out.ok = rep.ok; out.output = rep.result;
        }
    } catch (const std::exception& e) { out.error = e.what(); }
    catch (...) { out.error = "eval failed"; }
    return out;
}

YamBridge::LoadResult YamBridge::loadScript(const std::string& name,
                                              const std::string& code) {
    // Fire-and-forget (UI must not block on CV wait). Results arrive as
    // "user_script_loaded" / "script_failed" events.
    LoadResult out;
    if (!ready_.load()) { out.error = "bridge not ready"; return out; }
    try {
        auto r = yam::JavaScriptBridge::instance().run_user_script(name, code);
        if (!r) { out.error = r.error_message(); return out; }
        out.ok = true;
    } catch (const std::exception& e) { out.error = e.what(); }
    return out;
}

YamBridge::LoadResult YamBridge::loadScriptSync(const std::string& name,
                                                  const std::string& code,
                                                  int timeout_ms) {
    LoadResult out;
    if (!ready_.load()) { out.error = "bridge not ready"; return out; }

    {
        std::lock_guard<std::mutex> lk(g_scriptLoadMutex);
        g_scriptLoadResult[name] = {false, false};
        g_scriptLoadError[name].clear();
    }

    try {
        auto r = yam::JavaScriptBridge::instance().run_user_script(name, code);
        if (!r) { out.error = r.error_message(); return out; }
    } catch (const std::exception& e) {
        out.error = e.what();
        return out;
    }

    std::unique_lock<std::mutex> lk(g_scriptLoadMutex);
    bool got = g_scriptLoadCv.wait_for(lk,
        std::chrono::milliseconds(timeout_ms),
        [&]{ auto it = g_scriptLoadResult.find(name);
             return it != g_scriptLoadResult.end() && it->second.first; });

    if (got) {
        auto it = g_scriptLoadResult.find(name);
        out.ok = it->second.second;
        if (!out.ok) out.error = g_scriptLoadError[name];
    } else {
        out.error = "timeout waiting for '" + name + "' load result";
    }

    g_scriptLoadResult.erase(name);
    g_scriptLoadError.erase(name);

    if (out.ok) {
        std::lock_guard<std::mutex> lk2(mu_);
        bool found = false;
        for (auto& n : scriptNames_) if (n == name) { found = true; break; }
        if (!found) scriptNames_.push_back(name);
    }
    return out;
}

bool YamBridge::unloadScript(const std::string& name) {
    if (!ready_.load()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = scriptNames_.begin(); it != scriptNames_.end(); ++it) {
        if (*it == name) {
            scriptNames_.erase(it);
            LOGI("unloadScript: '%s' removed from list "
                 "(JS hooks remain — use Unload All to clear)", name.c_str());
            return true;
        }
    }
    return false;
}

bool YamBridge::unloadAllScripts() {
    if (!ready_.load()) return false;
    try {
        yam::JavaScriptBridge::instance().clear_user_scripts();
    } catch (...) { return false; }
    std::lock_guard<std::mutex> lk(mu_);
    scriptNames_.clear();
    LOGI("unloadAllScripts: cleared all scripts");
    return true;
}

std::vector<std::string> YamBridge::listScripts() const {
    std::lock_guard<std::mutex> lk(mu_);
    return scriptNames_;
}

void YamBridge::postEval(const std::string& code) {
    if (!ready_.load()) return;
    try { yam::JavaScriptBridge::instance().eval(code); } catch (...) {}
}

void YamBridge::setOutputSink(void (*sink)(const std::string&)) {
    outputSink_ = sink;
}

void YamBridge::setErrorSink(void (*sink)(const std::string&)) {
    errorSink_ = sink;
}

} // namespace yamgg
