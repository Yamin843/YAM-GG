#include "YamBridge.h"
#include "../Core/Runtime.h"
#include "../UI/Tabs/JSConsole.h"

#include "yam.hpp"

#include <android/log.h>
#include <chrono>
#include <condition_variable>
#include <unordered_map>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {
std::mutex                                              g_scriptLoadMutex;
std::condition_variable                                 g_scriptLoadCv;
std::unordered_map<std::string, std::pair<bool,bool>>   g_scriptLoadResult; // name → {done, ok}
std::unordered_map<std::string, std::string>            g_scriptLoadError;  // name → error
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
        LOGI("YAM::init failed: %s", r.error_message().c_str());
        return false;
    }

    auto& bridge = yam::JavaScriptBridge::instance();
    if (!bridge.is_ready()) {
        LOGI("bridge not ready after YAM::init");
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

    installEventRouter();

    yam::events::on("user_script_loaded", [](const yam::Event& ev) {
        std::string nm = ev.get_str("name");
        std::lock_guard<std::mutex> lk(g_scriptLoadMutex);
        // حد أعلى حتى لا تنمو الخرائط بلا نهاية عند تسريب اسم
        if (g_scriptLoadResult.size() > 64) {
            g_scriptLoadResult.clear();
            g_scriptLoadError.clear();
        }
        g_scriptLoadResult[nm] = {true, ev.get_bool("ok", false)};
        g_scriptLoadError[nm]  = ev.get_str("error");
        g_scriptLoadCv.notify_all();
    });
    yam::events::on("script_failed", [](const yam::Event& ev) {
        std::string nm = ev.get_str("name");
        std::lock_guard<std::mutex> lk(g_scriptLoadMutex);
        if (g_scriptLoadResult.size() > 64) {
            g_scriptLoadResult.clear();
            g_scriptLoadError.clear();
        }
        g_scriptLoadResult[nm] = {true, false};
        g_scriptLoadError[nm]  = ev.get_str("error");
        g_scriptLoadCv.notify_all();
    });

    ready_.store(true);
    initialized_.store(true);
    LOGI("YamBridge: ready");
    return true;
}

void YamBridge::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;
    try {
        yam::YAM::shutdown();
    } catch (...) {}
    {
        std::lock_guard<std::mutex> lk2(g_scriptLoadMutex);
        g_scriptLoadResult.clear();
        g_scriptLoadError.clear();
    }
    ready_.store(false);
    initialized_.store(false);
}

void YamBridge::installEventRouter() {
    yam::events::on("user_script_loaded", [](const yam::Event& ev) {
        std::string name = ev.get_str("name");
        bool ok = ev.get_bool("ok", false);
        LOGI("script loaded: %s ok=%d", name.c_str(), ok ? 1 : 0);
    });

    yam::events::on("script_failed", [](const yam::Event& ev) {
        std::string name = ev.get_str("name");
        std::string err = ev.get_str("error");
        LOGE("script failed: %s: %s", name.c_str(), err.c_str());
    });

    yam::events::on("scripts_batch_done", [](const yam::Event& ev) {
        LOGI("batch done: ok=%lld fail=%lld",
             (long long)ev.get_i64("ok", 0),
             (long long)ev.get_i64("fail", 0));
    });

    yam::events::on("cpp_ready", [](const yam::Event&) {
        LOGI("bridge cpp_ready");
    });

    yam::events::on("agent_ready", [](const yam::Event&) {
        LOGI("bridge agent_ready");
    });
}

void YamBridge::onEvalResult(unsigned long long id, bool ok,
                              const std::string& result,
                              const std::string& error) {
    (void)id;

    std::shared_ptr<EvalSync> sync;
    {
        std::lock_guard<std::mutex> lk(evalMu_);
        if (!pendingEvals_.empty()) {
            sync = pendingEvals_.front();
            pendingEvals_.pop_front();
        }
    }

    if (sync) {
        std::lock_guard<std::mutex> lk(sync->mu);
        sync->done = true;
        sync->ok = ok;
        sync->output = result;
        sync->error = error;
        sync->cv.notify_all();
        return;
    }

    if (ok) {
        if (!result.empty()) {
            JSConsole::instance().pushOutput(result);
        }
    } else if (!error.empty()) {
        JSConsole::instance().pushOutput("[error] " + error);
    }
}

void YamBridge::onConsole(const std::string& level, const std::string& line) {
    std::string prefix;
    if (level == "error")      prefix = "[error] ";
    else if (level == "warn")  prefix = "[warn] ";
    else                       prefix = "";

    JSConsole::instance().pushOutput(prefix + line);

    if (level == "error") {
        if (errorSink_) errorSink_(line);
        LOGE("[js] %s", line.c_str());
    } else {
        if (outputSink_) outputSink_(line);
        LOGI("[js] %s", line.c_str());
    }
}

YamBridge::EvalResult YamBridge::evaluate(const std::string& code) {
    EvalResult out;
    if (!ready_.load()) {
        out.error = "bridge not ready";
        return out;
    }

    auto sync = std::make_shared<EvalSync>();
    {
        std::lock_guard<std::mutex> lk(evalMu_);
        pendingEvals_.push_back(sync);
    }

    try {
        auto r = yam::JavaScriptBridge::instance().eval(code);
        if (!r) {
            std::lock_guard<std::mutex> lk(evalMu_);
            auto it = std::find(pendingEvals_.begin(), pendingEvals_.end(), sync);
            if (it != pendingEvals_.end()) pendingEvals_.erase(it);
            out.error = r.error_message();
            return out;
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(evalMu_);
        auto it = std::find(pendingEvals_.begin(), pendingEvals_.end(), sync);
        if (it != pendingEvals_.end()) pendingEvals_.erase(it);
        out.error = e.what();
        return out;
    }

    {
        std::unique_lock<std::mutex> lk(sync->mu);
        sync->cv.wait_for(lk, std::chrono::seconds(5),
                          [&] { return sync->done; });
    }

    {
        std::lock_guard<std::mutex> lk(evalMu_);
        auto it = std::find(pendingEvals_.begin(), pendingEvals_.end(), sync);
        if (it != pendingEvals_.end()) pendingEvals_.erase(it);
    }

    out.ok = sync->ok;
    out.output = sync->output;
    out.error = sync->error;
    if (!sync->done) out.error = "timeout";
    return out;
}

YamBridge::LoadResult YamBridge::loadScript(const std::string& name,
                                              const std::string& code) {
    LoadResult out;
    if (!ready_.load()) {
        out.error = "bridge not ready";
        return out;
    }

    try {
        auto r = yam::JavaScriptBridge::instance().run_user_script(name, code);
        if (!r) {
            out.error = r.error_message();
            return out;
        }
        out.ok = true;
        std::lock_guard<std::mutex> lk(mu_);
        bool found = false;
        for (auto& n : scriptNames_) if (n == name) { found = true; break; }
        if (!found) scriptNames_.push_back(name);
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

bool YamBridge::unloadScript(const std::string& name) {
    if (!ready_.load()) return false;
    try {
        // Ask JS side to unload user scripts (the java-bridge exposes this).
        yam::JavaScriptBridge::instance().clear_user_scripts();
    } catch (...) {
        return false;
    }
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = scriptNames_.begin(); it != scriptNames_.end(); ++it) {
        if (*it == name) {
            scriptNames_.erase(it);
            return true;
        }
    }
    return true;
}

std::vector<std::string> YamBridge::listScripts() const {
    std::lock_guard<std::mutex> lk(mu_);
    return scriptNames_;
}

void YamBridge::postEval(const std::string& code) {
    if (!ready_.load()) return;
    try {
        yam::JavaScriptBridge::instance().eval(code);
    } catch (...) {}
}

YamBridge::LoadResult YamBridge::loadScriptSync(const std::string& name,
                                                   const std::string& code,
                                                   int timeout_ms) {
    LoadResult out;
    if (!ready_.load()) { out.error = "bridge not ready"; return out; }

    // Reset state for this name
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
    return out;
}

void YamBridge::setOutputSink(void (*sink)(const std::string&)) {
    outputSink_ = sink;
}

void YamBridge::setErrorSink(void (*sink)(const std::string&)) {
    errorSink_ = sink;
}

} // namespace yamgg
