#include "YamBridge.h"
#include <thread>
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

    // Hook lifecycle → forward to the user callback if set.
    bridge.set_hook_callback([this](
        yam::i64 cbId, const yam::String& phase,
        const std::vector<yam::u64>& args,
        yam::u64 thisH, yam::u64 retH,
        bool isVoid, const yam::String& exMsg)
    {
        if (hookCb_) {
            try {
                hookCb_(cbId, phase, args, thisH, retH, isVoid, exMsg);
            } catch (const std::exception& e) {
                LOGE("hook cb: %s", e.what());
            }
        }
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
        // لا cap — كل loadScriptSync يزيل عنصره عند الاكتمال/الـ timeout،
        // فلا تسريب طبيعي. الحجم كبير فقط إذا كانت خرائط loadScript
        // معلّقة (نادرة).
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

    // ─── Watchdog: every 60s, warn if Java hook count is suspicious.
    // On 17.22.x the interceptor leak can accumulate. We can't fix the
    // library, but we can warn the user before it becomes a problem.
    {
        std::thread watchdog([]() {
            using namespace std::chrono;
            while (true) {
                std::this_thread::sleep_for(seconds(60));
                if (!YamBridge::instance().isReady()) break;
                try {
                    auto n = yam::JavaHookManager::instance().size();
                    if (n >= 256) {
                        __android_log_print(ANDROID_LOG_WARN, "YAMGG",
                            "hook watchdog: %zu java hooks active — consider "
                            "unhooking idle methods (interceptor leak on 17.22.x)",
                            n);
                    }
                } catch (...) {}
            }
        });
        watchdog.detach();
        LOGI("YamBridge: hook watchdog started (60s)");
    }

    return true;
}

void YamBridge::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;

    // ─── 1. امسح callbacks أولاً — نمنع وصول أي استدعاء من الجسر
    //            بعد أن نُفكّك الحالة.
    try {
        auto& bridge = yam::JavaScriptBridge::instance();
        bridge.set_hook_callback(nullptr);
        bridge.set_console_callback(nullptr);
        bridge.set_eval_callback(nullptr);
    } catch (...) {}

    // ─── 2. أزل hookCb_ المحلي
    hookCb_ = nullptr;

    // ─── 2b. حرّر كل Java hooks قبل إنهاء الجسر. هذا يمنع تسريب
    //        مراجع بين interceptor و hooks داخل الجسر المدمج.
    try {
        yam::JavaHookManager::instance().clear();
    } catch (...) {}

    // ─── 2c. أوقف interceptor كاملاً
    try {
        auto& ic = yam::Interceptor::instance();
        (void)ic;
    } catch (...) {}

    // ─── 3. أوقف YAM
    try {
        yam::YAM::shutdown();
    } catch (...) {}

    // ─── 4. افرغ خرائط السكربتات
    {
        std::lock_guard<std::mutex> lk2(g_scriptLoadMutex);
        g_scriptLoadResult.clear();
        g_scriptLoadError.clear();
    }

    {
        std::lock_guard<std::mutex> lk3(mu_);
        scriptNames_.clear();
    }

    ready_.store(false);
    initialized_.store(false);
}

void YamBridge::installEventRouter() {
    // Idempotent: even if called multiple times (from YAM::init and again
    // from YamBridge::initialize), the handlers register exactly once.
    static std::once_flag once_;
    std::call_once(once_, []() {
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
    }); // end call_once
}

void YamBridge::onEvalResult(unsigned long long id, bool ok,
                              const std::string& result,
                              const std::string& error) {
    (void)id;

    // eval is now synchronous through JavaScriptBridge::eval() — the
    // eval_result event is informational only. We simply forward it to
    // the on-screen console so the user can see what happened.
    //
    // Guarded: this callback may fire before the UI thread has created
    // the JSConsole singleton. pushOutput() touches a mutex, which is
    // fine on any thread — but a full UI push before ImGui_ImplOpenGL3
    // has initialized could deadlock. We check ready state.
    try {
        if (ok) {
            if (!result.empty()) {
                JSConsole::instance().pushOutput(result);
            }
        } else if (!error.empty()) {
            JSConsole::instance().pushOutput("[error] " + error);
        }
    } catch (const std::exception& e) {
        LOGE("onEvalResult: %s", e.what());
    } catch (...) {
        LOGE("onEvalResult: unknown exception");
    }
}

void YamBridge::onConsole(const std::string& level, const std::string& line) {
    std::string prefix;
    if (level == "error")      prefix = "[error] ";
    else if (level == "warn")  prefix = "[warn] ";
    else                       prefix = "";

    // Same early-init concern as onEvalResult. Guard everything.
    try {
        JSConsole::instance().pushOutput(prefix + line);
    } catch (...) {
        // If the console isn't up yet, fall through to logcat.
    }

    if (level == "error") {
        if (errorSink_) {
            try { errorSink_(line); } catch (...) {}
        }
        LOGE("[js] %s", line.c_str());
    } else {
        if (outputSink_) {
            try { outputSink_(line); } catch (...) {}
        }
        LOGI("[js] %s", line.c_str());
    }
}

YamBridge::EvalResult YamBridge::evaluate(const std::string& code) {
    EvalResult out;

    if (!ready_.load()) {
        out.error = "bridge not ready";
        return out;
    }

    // JavaScriptBridge::eval already gives us a synchronous JavaReply
    // via the cpp_eval command (which sends a real "reply" envelope back).
    // We do NOT need a separate callback/CV path here — that path had a
    // race: JavaScriptBridge::eval returns, then the eval_result event
    // also arrives asynchronously and could be assigned to the wrong
    // pending sync if multiple evals are in flight.
    try {
        auto r = yam::JavaScriptBridge::instance().eval(code);
        if (!r) {
            out.error = r.error_message();
            return out;
        }
        const auto& rep = r.value();
        if (rep.kind == "value" || rep.kind == "pong") {
            out.ok = true;
            out.output = rep.result;
        } else if (rep.kind == "null") {
            out.ok = true;
            out.output = "null";
        } else if (rep.kind == "void") {
            out.ok = true;
            out.output = "";
        } else if (rep.kind == "error" || !rep.error.empty()) {
            out.ok = false;
            out.error = rep.error.empty() ? "unknown eval error" : rep.error;
        } else {
            out.ok = rep.ok;
            out.output = rep.result;
        }
    } catch (const std::exception& e) {
        out.error = e.what();
    } catch (...) {
        out.error = "eval failed (unknown exception)";
    }

    return out;
}

YamBridge::LoadResult YamBridge::loadScript(const std::string& name,
                                              const std::string& code) {
    // الاسم يبقى للتوافق — يستخدم النسخة المتزامنة داخلياً.
    // الآن loadScript و loadScriptSync لهما نفس السلوك الصحيح.
    return loadScriptSync(name, code, 8000);
}

bool YamBridge::unloadScript(const std::string& name) {
    if (!ready_.load()) return false;

    // IMPORTANT: frida-java-bridge has NO per-script unload API. The only
    // way to remove JS code is clear_user_scripts() which kills ALL scripts.
    //
    // To honor individual-remove semantics, we only forget the name here.
    // The JS hooks installed by that script stay active until the user
    // explicitly unloads all — this is a documented bridge limitation,
    // not a bug in our code. The UI (JSConsole) warns the user.
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = scriptNames_.begin(); it != scriptNames_.end(); ++it) {
        if (*it == name) {
            scriptNames_.erase(it);
            LOGI("unloadScript: '%s' removed from list "
                 "(JS hooks remain — use Unload All to fully clear)",
                 name.c_str());
            return true;
        }
    }
    return false;
}

bool YamBridge::unloadAllScripts() {
    if (!ready_.load()) return false;
    try {
        yam::JavaScriptBridge::instance().clear_user_scripts();
    } catch (...) {
        return false;
    }
    std::lock_guard<std::mutex> lk(mu_);
    scriptNames_.clear();
    LOGI("unloadAllScripts: cleared all scripts on the bridge");
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

    if (out.ok) {
        std::lock_guard<std::mutex> lk(mu_);
        bool found = false;
        for (auto& n : scriptNames_) if (n == name) { found = true; break; }
        if (!found) scriptNames_.push_back(name);
    }
    return out;
}

void YamBridge::setOutputSink(void (*sink)(const std::string&)) {
    outputSink_ = sink;
}

void YamBridge::setErrorSink(void (*sink)(const std::string&)) {
    errorSink_ = sink;
}

} // namespace yamgg
