#include "YamBridge.h"
#include "../Core/Runtime.h"
#include "../UI/Tabs/JSConsole.h"

#include "yam.hpp"

#include <android/log.h>
#include <chrono>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

YamBridge::YamBridge() = default;
YamBridge::~YamBridge() = default;

YamBridge& YamBridge::instance() {
    static YamBridge inst;
    return inst;
}

bool YamBridge::initialize() {
    #define DBG(...) __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG", __VA_ARGS__)

    DBG(">>> [1] entered");
    std::lock_guard<std::mutex> lk(mu_);
    DBG(">>> [2] mutex");

    if (initialized_.load()) { DBG(">>> [3] already"); return true; }
    if (!Runtime::instance().isInitialized()) { DBG(">>> [3b] no runtime"); return false; }
    DBG(">>> [4] runtime ok");

    yam::EntryOptions opts;
    opts.log_level = yam::LogLevel::Debug;
    opts.enable_java = true;
    opts.enable_console = false;
    opts.enable_debugger = false;
    opts.worker_threads = 0;
    opts.script_name = "yamgg_bridge";
    opts.backend = yam::BackendKind::QJS;
    DBG(">>> [5] calling YAM::init");

    auto r = yam::YAM::init(opts);
    DBG(">>> [6] YAM::init returned ok=%d", (int)r.has_value());
    if (!r) { DBG(">>> [6b] %s", r.error_message().c_str()); return false; }

    DBG(">>> [7] getting bridge");
    auto& bridge = yam::JavaScriptBridge::instance();
    DBG(">>> [8] got bridge");

    bridge.set_console_callback([this](const yam::String& l, const yam::String& s) {
        onConsole(l, s);
    });
    DBG(">>> [9] console cb");

    bridge.set_eval_callback([this](yam::u64 id, bool ok,
                                     const yam::String& res,
                                     const yam::String& err) {
        onEvalResult(id, ok, res, err);
    });
    DBG(">>> [10] eval cb");

    DBG(">>> [11] checking bridge.is_ready() — YAM::init already tried");
    if (!bridge.is_ready()) {
        DBG(">>> [11b] bridge NOT ready after YAM::init; leaving it up, returning false");
        // Do NOT call bridge.initialize() again — that reassigns script_ and
        // triggers the previous Script destructor while callbacks are in flight.
            return false;
    }
    DBG(">>> [12] bridge is ready");

    DBG(">>> [13] installEventRouter");
    installEventRouter();
    DBG(">>> [14] router installed");

    ready_.store(true);
    initialized_.store(true);
    DBG(">>> [15] READY");
    return true;
}

void YamBridge::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;
    try {
        yam::YAM::shutdown();
    } catch (...) {}
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
        sync = pendingEval_;
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
        pendingEval_ = sync;
    }

    try {
        auto r = yam::JavaScriptBridge::instance().eval(code);
        if (!r) {
            std::lock_guard<std::mutex> lk(evalMu_);
            pendingEval_.reset();
            out.error = r.error_message();
            return out;
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(evalMu_);
        pendingEval_.reset();
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
        if (pendingEval_ == sync) pendingEval_.reset();
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
        std::string code =
            "(function(){try{"
            "if(globalThis.__yamgg_scripts&&globalThis.__yamgg_scripts['" + name + "'])"
            "{delete globalThis.__yamgg_scripts['" + name + "'];}}"
            "catch(e){}})();";
        yam::JavaScriptBridge::instance().eval(code);
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

void YamBridge::setOutputSink(void (*sink)(const std::string&)) {
    outputSink_ = sink;
}

void YamBridge::setErrorSink(void (*sink)(const std::string&)) {
    errorSink_ = sink;
}

} // namespace yamgg
