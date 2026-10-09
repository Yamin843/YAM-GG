#ifndef YAMGG_BRIDGE_YAMBRIDGE_H
#define YAMGG_BRIDGE_YAMBRIDGE_H

#include <string>
#include <functional>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <vector>
#include <deque>
#include <condition_variable>

namespace yamgg {

class YamBridge {
public:
    struct EvalResult {
        bool ok{false};
        std::string output;
        std::string error;
    };

    struct LoadResult {
        bool ok{false};
        std::string error;
    };

    static YamBridge& instance();

    bool initialize();
    void shutdown();
    bool isReady() const { return ready_.load(); }

    EvalResult evaluate(const std::string& code);
    void postEval(const std::string& code);   // fire-and-forget
    LoadResult loadScript(const std::string& name, const std::string& code);
    LoadResult loadScriptSync(const std::string& name, const std::string& code,
                               int timeout_ms = 8000);
    bool unloadScript(const std::string& name);
    std::vector<std::string> listScripts() const;

    void installEventRouter();

    // Bridge hook lifecycle callback (enter/leave/exception).
    // NOTE: use std::int64_t/std::uint64_t — NOT long long — so that
    // the types exactly match yam::i64/yam::u64 which are typedefs for
    // std::int64_t/std::uint64_t. On aarch64-linux-android these are
    // "long"/"unsigned long" (not "long long"), and the two are distinct
    // C++ types even though both are 64-bit.
    using HookCb = std::function<void(
        std::int64_t /*cbId*/, const std::string& /*phase*/,
        const std::vector<std::uint64_t>& /*args*/,
        std::uint64_t /*thisH*/,
        std::uint64_t /*retH*/,
        bool /*isVoid*/,
        const std::string& /*exMsg*/)>;
    void setHookCb(HookCb cb) { hookCb_ = std::move(cb); }
    void setOutputSink(void (*sink)(const std::string&));
    void setErrorSink(void (*sink)(const std::string&));

private:
    YamBridge();
    ~YamBridge();
    YamBridge(const YamBridge&) = delete;
    YamBridge& operator=(const YamBridge&) = delete;

    // EvalSync removed — eval is synchronous now

    void onEvalResult(unsigned long long id, bool ok,
                      const std::string& result, const std::string& error);
    void onConsole(const std::string& level, const std::string& line);

    std::atomic<bool> ready_{false};
    std::atomic<bool> initialized_{false};
    mutable std::mutex mu_;
    std::vector<std::string> scriptNames_;
    // (eval is now synchronous via JavaScriptBridge::eval — no queue needed)
    HookCb hookCb_;
    void (*outputSink_)(const std::string&){nullptr};
    void (*errorSink_)(const std::string&){nullptr};
};

#define YAMGG_YAMBRIDGE yamgg::YamBridge::instance()

} // namespace yamgg

#endif
