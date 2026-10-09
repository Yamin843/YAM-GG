// ===========================================================================
// yam_core.cpp
// ---------------------------------------------------------------------------
// Foundation: error, log, string, time, thread pool, runtime, script,
// message, cancellable, memory, module, symbol, range, memory patch,
// registry, gc, diag.
// ===========================================================================

#include "yam_internal.hpp"

#include <android/log.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <cxxabi.h>

// ===========================================================================
// namespace yam
// ===========================================================================

namespace yam {

// ===========================================================================
// SECTION 1 — ErrorCategory / error_code / throw_error
// ===========================================================================

std::string ErrorCategory::message(int ev) const {
    switch (static_cast<ErrorCode>(ev)) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::Unknown: return "unknown error";
    case ErrorCode::InvalidArgument: return "invalid argument";
    case ErrorCode::OutOfRange: return "out of range";
    case ErrorCode::NotInitialized: return "not initialized";
    case ErrorCode::AlreadyInitialized: return "already initialized";
    case ErrorCode::ScriptCompileFailed: return "script compile failed";
    case ErrorCode::ScriptLoadFailed: return "script load failed";
    case ErrorCode::ScriptUnloadFailed: return "script unload failed";
    case ErrorCode::BackendUnavailable: return "backend unavailable";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::NotAttached: return "not attached";
    case ErrorCode::AttachFailed: return "attach failed";
    case ErrorCode::DetachFailed: return "detach failed";
    case ErrorCode::ReplaceFailed: return "replace failed";
    case ErrorCode::RevertFailed: return "revert failed";
    case ErrorCode::MemoryAccessDenied: return "memory access denied";
    case ErrorCode::MemoryReadFailed: return "memory read failed";
    case ErrorCode::MemoryWriteFailed: return "memory write failed";
    case ErrorCode::MemoryAllocFailed: return "memory alloc failed";
    case ErrorCode::ModuleNotFound: return "module not found";
    case ErrorCode::SymbolNotFound: return "symbol not found";
    case ErrorCode::ClassNotFound: return "class not found";
    case ErrorCode::MethodNotFound: return "method not found";
    case ErrorCode::FieldNotFound: return "field not found";
    case ErrorCode::OverloadAmbiguous: return "overload ambiguous";
    case ErrorCode::OverloadNotFound: return "overload not found";
    case ErrorCode::JniNotAttached: return "jni not attached";
    case ErrorCode::JniExceptionPending: return "jni exception pending";
    case ErrorCode::JavaVmNotFound: return "java vm not found";
    case ErrorCode::TypeError: return "type error";
    case ErrorCode::NullDereference: return "null dereference";
    case ErrorCode::NotImplemented: return "not implemented";
    case ErrorCode::InternalError: return "internal error";
    }
    return "unknown";
}

const std::error_category& error_category() noexcept {
    static ErrorCategory cat;
    return cat;
}

std::error_code make_error_code(ErrorCode c) noexcept {
    return { static_cast<int>(c), error_category() };
}

[[noreturn]] void throw_error(ErrorCode c, const String& m) {
    throw Error(c, m);
}

[[noreturn]] void throw_error(ErrorCode c, String&& m) {
    throw Error(c, std::move(m));
}

// ===========================================================================
// SECTION 2 — Log
// ===========================================================================

namespace {
std::atomic<int> g_log_level{ static_cast<int>(LogLevel::Info) };
std::mutex       g_log_mu;
LogCallback      g_log_cb;

const char* log_level_tag(LogLevel lv) {
    switch (lv) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    case LogLevel::Off:   return "OFF  ";
    }
    return "?????";
}

int log_android_prio(LogLevel lv) {
    switch (lv) {
    case LogLevel::Trace: return ANDROID_LOG_VERBOSE;
    case LogLevel::Debug: return ANDROID_LOG_DEBUG;
    case LogLevel::Info:  return ANDROID_LOG_INFO;
    case LogLevel::Warn:  return ANDROID_LOG_WARN;
    case LogLevel::Error: return ANDROID_LOG_ERROR;
    case LogLevel::Fatal: return ANDROID_LOG_FATAL;
    default:              return ANDROID_LOG_DEFAULT;
    }
}

void log_emit(LogLevel lv, const String& msg) {
    if (static_cast<int>(lv) < g_log_level.load(std::memory_order_acquire)) return;
    __android_log_print(log_android_prio(lv), "YAM", "[%s] %s",
                        log_level_tag(lv), msg.c_str());
    LogCallback cb;
    {
        std::lock_guard<std::mutex> lk(g_log_mu);
        cb = g_log_cb;
    }
    if (cb) {
        try { cb(lv, msg); } catch (...) {}
    }
}

bool g_trace_enabled = false;
}

void Log::set_level(LogLevel lv) noexcept {
    g_log_level.store(static_cast<int>(lv), std::memory_order_release);
}
LogLevel Log::level() noexcept {
    return static_cast<LogLevel>(g_log_level.load(std::memory_order_acquire));
}
void Log::set_callback(LogCallback cb) {
    std::lock_guard<std::mutex> lk(g_log_mu);
    g_log_cb = std::move(cb);
}
void Log::clear_callback() {
    std::lock_guard<std::mutex> lk(g_log_mu);
    g_log_cb = nullptr;
}
void Log::trace(const String& m) { log_emit(LogLevel::Trace, m); }
void Log::debug(const String& m) { log_emit(LogLevel::Debug, m); }
void Log::info(const String& m)  { log_emit(LogLevel::Info, m); }
void Log::warn(const String& m)  { log_emit(LogLevel::Warn, m); }
void Log::error(const String& m) { log_emit(LogLevel::Error, m); }
void Log::fatal(const String& m) { log_emit(LogLevel::Fatal, m); }

namespace detail {

LogStream::LogStream(LogLevel lv, const char* file, int line)
    : lv_(lv), file_(file), line_(line) {}

LogStream::~LogStream() {
    if (static_cast<int>(lv_) < g_log_level.load(std::memory_order_acquire)) return;
    std::string s = ss_.str();
    const char* base = std::strrchr(file_, '/');
    if (base) file_ = base + 1;
    std::ostringstream final;
    final << '[' << file_ << ':' << line_ << "] " << s;
    log_emit(lv_, final.str());
}

void trace_enable(bool on) { g_trace_enabled = on; }
bool trace_enabled() noexcept { return g_trace_enabled; }
void trace(const char* fmt, ...) {
    if (!g_trace_enabled) return;
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_VERBOSE, "YAM-TRACE", "%s", buf);
}

} // namespace detail

// ===========================================================================
// SECTION 3 — String utilities
// ===========================================================================

namespace str {

bool starts_with(StringView s, StringView p) noexcept {
    return s.size() >= p.size() &&
           std::memcmp(s.data(), p.data(), p.size()) == 0;
}
bool ends_with(StringView s, StringView p) noexcept {
    return s.size() >= p.size() &&
           std::memcmp(s.data() + s.size() - p.size(), p.data(), p.size()) == 0;
}
bool contains(StringView s, StringView p) noexcept {
    return s.find(p) != StringView::npos;
}

String to_lower(StringView s) {
    String r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}
String to_upper(StringView s) {
    String r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return r;
}

static bool is_ws(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

String trim_left(StringView s) {
    size_t i = 0; while (i < s.size() && is_ws(s[i])) ++i;
    return String(s.substr(i));
}
String trim_right(StringView s) {
    size_t i = s.size(); while (i > 0 && is_ws(s[i - 1])) --i;
    return String(s.substr(0, i));
}
String trim(StringView s) { return trim_right(trim_left(s)); }

std::vector<String> split(StringView s, char d) {
    std::vector<String> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(d, start);
        if (p == StringView::npos) { out.emplace_back(s.substr(start)); break; }
        out.emplace_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

std::vector<String> split(StringView s, StringView d) {
    std::vector<String> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(d, start);
        if (p == StringView::npos) { out.emplace_back(s.substr(start)); break; }
        out.emplace_back(s.substr(start, p - start));
        start = p + d.size();
    }
    return out;
}

String join(const std::vector<String>& parts, StringView sep) {
    if (parts.empty()) return {};
    String r;
    size_t total = 0;
    for (auto& s : parts) total += s.size() + sep.size();
    r.reserve(total);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) r.append(sep.data(), sep.size());
        r.append(parts[i]);
    }
    return r;
}

String replace_all(StringView s, StringView from, StringView to) {
    if (from.empty()) return String(s);
    String r; r.reserve(s.size());
    size_t pos = 0;
    while (true) {
        size_t f = s.find(from, pos);
        if (f == StringView::npos) { r.append(s.substr(pos)); break; }
        r.append(s.substr(pos, f - pos));
        r.append(to.data(), to.size());
        pos = f + from.size();
    }
    return r;
}

String vformat(const char* fmt, va_list ap) {
    va_list cp; va_copy(cp, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, cp);
    va_end(cp);
    if (n < 0) return {};
    String out;
    out.resize(static_cast<size_t>(n));
    std::vsnprintf(out.data(), static_cast<size_t>(n) + 1, fmt, ap);
    return out;
}

String format(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    String s = vformat(fmt, ap);
    va_end(ap);
    return s;
}

String hex(u64 v, bool prefix, int width) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%0*llx",
                          width, static_cast<unsigned long long>(v));
    String s;
    if (prefix) s += "0x";
    s.append(buf, static_cast<size_t>(n));
    return s;
}

String hex(const void* p, bool prefix) {
    return hex(reinterpret_cast<u64>(p), prefix, 0);
}

bool parse_i64(StringView s, i64& out, int base) noexcept {
    if (s.empty()) return false;
    String tmp(s);
    char* end = nullptr;
    errno = 0;
    long long v = std::strtoll(tmp.c_str(), &end, base);
    if (errno != 0 || end == tmp.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_u64(StringView s, u64& out, int base) noexcept {
    if (s.empty()) return false;
    String tmp(s);
    char* end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(tmp.c_str(), &end, base);
    if (errno != 0 || end == tmp.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_f64(StringView s, f64& out) noexcept {
    if (s.empty()) return false;
    String tmp(s);
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(tmp.c_str(), &end);
    if (errno != 0 || end == tmp.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

String jni_to_dotted(StringView jni) {
    if (jni.empty()) return {};
    String s(jni);
    if (s.front() == 'L' && s.back() == ';') s = s.substr(1, s.size() - 2);
    for (char& c : s) if (c == '/') c = '.';
    return s;
}

String dotted_to_jni(StringView dotted) {
    String s(dotted);
    for (char& c : s) if (c == '.') c = '/';
    if (!s.empty() && s.front() != '[') return "L" + s + ";";
    return s;
}

String dotted_to_path(StringView dotted) {
    String s(dotted);
    for (char& c : s) if (c == '.') c = '/';
    return s;
}

String escape_json(StringView s) {
    String o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else o += c;
        }
    }
    return o;
}

String unescape_json(StringView s) {
    String o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '\\') { o += c; continue; }
        if (i + 1 >= s.size()) break;
        char n = s[++i];
        switch (n) {
        case '"': o += '"'; break;
        case '\\': o += '\\'; break;
        case '/': o += '/'; break;
        case 'n': o += '\n'; break;
        case 'r': o += '\r'; break;
        case 't': o += '\t'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'u': {
            if (i + 4 >= s.size()) { o += '?'; break; }
            char hex[5] = { s[i+1], s[i+2], s[i+3], s[i+4], 0 };
            unsigned long cp = std::strtoul(hex, nullptr, 16);
            i += 4;
            if (cp < 0x80) o += static_cast<char>(cp);
            else if (cp < 0x800) {
                o += static_cast<char>(0xC0 | (cp >> 6));
                o += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                o += static_cast<char>(0xE0 | (cp >> 12));
                o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                o += static_cast<char>(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: o += n;
        }
    }
    return o;
}

} // namespace str

// ===========================================================================
// SECTION 4 — Time
// ===========================================================================

namespace time_util {

i64 now_ms() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
i64 now_us() noexcept {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}
i64 now_ns() noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

String iso8601(i64 ms) {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace time_util

// ===========================================================================
// SECTION 5 — ThreadPool
// ===========================================================================

ThreadPool::ThreadPool(usize n) {
    if (n == 0) n = std::max<usize>(2, std::thread::hardware_concurrency());
    threads_.reserve(n);
    for (usize i = 0; i < n; ++i) {
        threads_.emplace_back([this]{ worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
    for (auto& t : threads_) if (t.joinable()) t.join();
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lk(mu_);
    done_cv_.wait(lk, [this]{ return queue_.empty() && active_ == 0; });
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this]{ return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop();
        }
        try { job(); }
        catch (const std::exception& e) { YAM_LOG_ERROR() << "worker: " << e.what(); }
        catch (...) { YAM_LOG_ERROR() << "worker: unknown"; }
        {
            std::lock_guard<std::mutex> lk(mu_);
            --active_;
            if (queue_.empty() && active_ == 0) done_cv_.notify_all();
        }
    }
}

// ===========================================================================
// SECTION 6 — Runtime
// ===========================================================================

namespace {
std::atomic<bool> g_yam_inited{false};
std::mutex        g_yam_mutex;
}

Runtime::Runtime() = default;
Runtime::~Runtime() { if (initialized_) shutdown(); }

Runtime& Runtime::instance() {
    static Runtime inst;
    return inst;
}

Result<void> Runtime::init() {
    RuntimeOptions o;
    return init(o);
}

Result<void> Runtime::init(const RuntimeOptions& opt) {
    std::lock_guard<std::mutex> lk(mu_);
    if (initialized_) return Result<void>::err(ErrorCode::AlreadyInitialized, "");
    opts_ = opt;
    Log::set_level(opts_.log_level);

    bool expected = false;
    if (!g_yam_inited.compare_exchange_strong(expected, true)) {
        return Result<void>::err(ErrorCode::AlreadyInitialized, "yam already inited");
    }

    try {
        yam_init_embedded();
        backend_ = yam_script_backend_obtain_qjs();
        if (!backend_) {
            yam_deinit_embedded();
            g_yam_inited.store(false);
            return Result<void>::err(ErrorCode::BackendUnavailable, "qjs backend unavailable");
        }
        initialized_ = true;
        YAM_LOG_INFO() << "runtime initialized";
        return Result<void>::ok();
    } catch (const std::exception& e) {
        g_yam_inited.store(false);
        return Result<void>::err(ErrorCode::InternalError, e.what());
    }
}

Result<void> Runtime::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_) return Result<void>::err(ErrorCode::NotInitialized, "");
    try {
        yam_deinit_embedded();
        g_yam_inited.store(false);
        initialized_ = false;
        backend_ = nullptr;
        YAM_LOG_INFO() << "runtime shutdown";
        return Result<void>::ok();
    } catch (const std::exception& e) {
        return Result<void>::err(ErrorCode::InternalError, e.what());
    }
}

// ===========================================================================
// SECTION 7 — Message / MessageParser
// ===========================================================================

namespace {
const char* skip_ws(const char* p, const char* e) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}
} // namespace

bool MessageParser::parse(const String& raw, Message& out) {
    const char* p = raw.data();
    const char* e = p + raw.size();
    const char* vb = nullptr;
    const char* ve = nullptr;
    bool is_str = false;

    // type
    {
        String k = "\"type\"";
        StringView sv(p, raw.size());
        size_t pos = sv.find(k);
        if (pos == StringView::npos) return false;
        const char* q = p + pos + k.size();
        q = skip_ws(q, e);
        if (q >= e || *q != ':') return false;
        ++q; q = skip_ws(q, e);
        if (*q == '"') {
            ++q;
            vb = q;
            while (q < e && *q != '"') { if (*q == '\\' && q + 1 < e) q += 2; else ++q; }
            ve = q;
            is_str = true;
        }
    }
    out.type.assign(vb, ve);
    (void)is_str;

    // payload (optional)
    {
        String k = "\"payload\"";
        StringView sv(p, raw.size());
        size_t pos = sv.find(k);
        if (pos != StringView::npos) {
            const char* q = p + pos + k.size();
            q = skip_ws(q, e);
            if (q < e && *q == ':') {
                ++q; q = skip_ws(q, e);
                if (q < e && *q == '"') {
                    ++q;
                    vb = q;
                    while (q < e && *q != '"') { if (*q == '\\' && q + 1 < e) q += 2; else ++q; }
                    out.payload.assign(vb, q);
                }
            }
        }
    }

    out.timestamp = time_util::now_ms();
    return true;
}

// ===========================================================================
// SECTION 8 — Cancellable
// ===========================================================================

namespace {
std::atomic<bool> g_cancel_flag{false};
}

Cancellable::Cancellable() { g_cancel_flag.store(false); }
Cancellable::~Cancellable() = default;
void Cancellable::cancel() { g_cancel_flag.store(true); }
bool Cancellable::is_cancelled() const noexcept { return g_cancel_flag.load(); }

// ===========================================================================
// SECTION 9 — Script
// ===========================================================================

namespace {
// Trampoline: the Yam engine calls a C function with (msg, data, user).
// We forward into the Script instance.
void script_msg_trampoline(const gchar* msg, GBytes* data, gpointer user) {
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        ">>> TRAMPOLINE CALLED msg=%s",
        msg ? msg : "(null)");

    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        "TRAMPOLINE CALLED: msg=%s", msg ? msg : "(null)");
    auto* self = static_cast<Script*>(user);
    if (!self) {
        __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
            "TRAMPOLINE: self is null");
        return;
    }
    ByteVector bytes;
    if (data) {
        // no g_bytes_get_data binding
    }
    self->dispatch_message(msg ? String(msg) : String(), bytes);
}
} // namespace

Script::Script(const String& n, const String& s, const ByteVector& b)
    : name_(n), source_(s), source_bytes_(b) {}

Script::~Script() {
    if (loaded_) { try { unload(); } catch (...) {} }
    if (handle_) {
        yam_object_unref(handle_);
        handle_ = nullptr;
    }
}

Ptr<Script> Script::create(const String& n, const String& s) {
    return Ptr<Script>(new Script(n, s, {}));
}
Ptr<Script> Script::create(const String& n, const String& s, const ByteVector& b) {
    return Ptr<Script>(new Script(n, s, b));
}

Result<void> Script::load() { Cancellable c; return load(c); }

Result<void> Script::load(Cancellable& c) {
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        ">>> Script::load ENTER name=%s", name_.c_str());

    if (loaded_) return Result<void>::err(ErrorCode::InvalidArgument, "already loaded");
    auto& rt = Runtime::instance();
    if (!rt.is_initialized()) {
        return Result<void>::err(ErrorCode::NotInitialized, "runtime");
    }
    GError* err = nullptr;
    const gchar* nm = name_.empty() ? "yam" : name_.c_str();
    const gchar* src = source_.empty() ? "" : source_.c_str();
    handle_ = yam_script_backend_create_sync(
        static_cast<YamScriptBackend*>(rt.backend_handle()),
        nm, src, nullptr,
        reinterpret_cast<GCancellable*>(c.native_handle()),
        &err);
    if (!handle_) {
        return Result<void>::err(ErrorCode::ScriptCompileFailed, "create");
    }
    yam_script_set_message_handler(static_cast<YamScript*>(handle_),
        script_msg_trampoline, this, nullptr);
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        "before yam_script_load_sync");
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        ">>> BEFORE yam_script_load_sync");
    yam_script_load_sync(static_cast<YamScript*>(handle_),
        reinterpret_cast<GCancellable*>(c.native_handle()));
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        ">>> AFTER yam_script_load_sync");
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        "after yam_script_load_sync");

    // Pump the thread-default GMainContext so queued JS messages
    // (from send()) are actually delivered to our message handler.
    GMainContext* ctx = g_main_context_get_thread_default();
    if (!ctx) ctx = g_main_context_default();
    if (ctx) {
        __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
            "pumping GMainContext");
        for (int i = 0; i < 300; ++i) {
            while (g_main_context_iteration(ctx, FALSE)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
            "pump finished");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
            "NO GMainContext available");
    }

    loaded_ = true;
    YAM_LOG_INFO() << "script loaded: " << name_;
    return Result<void>::ok();
}

Result<void> Script::unload() { Cancellable c; return unload(c); }

Result<void> Script::unload(Cancellable& c) {
    if (!loaded_ || !handle_) return Result<void>::err(ErrorCode::NotInitialized, "");
    yam_script_unload_sync(static_cast<YamScript*>(handle_),
        reinterpret_cast<GCancellable*>(c.native_handle()));
    loaded_ = false;
    return Result<void>::ok();
}

void Script::set_message_handler(MessageCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mu_);
    cb_ = std::move(cb);
}
void Script::clear_message_handler() {
    std::lock_guard<std::mutex> lk(cb_mu_);
    cb_ = nullptr;
}
void Script::post(const String& msg) {
    if (!handle_) return;
    yam_script_post(static_cast<YamScript*>(handle_), msg.c_str(), nullptr);
}
void Script::post(const String& msg, const ByteVector& /* data */) {
    // Note: yam_script_post_with_data does not exist in the library. We fall
    // back to plain yam_script_post. If data is needed, encode it in the JSON
    // payload as base64.
    if (!handle_) return;
    yam_script_post(static_cast<YamScript*>(handle_), msg.c_str(), nullptr);
}

void Script::dispatch_message(const String& raw, const ByteVector& bytes) {
    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        "dispatch_message: raw=%s", raw.substr(0, 200).c_str());

    MessageCallback cb;
    {
        std::lock_guard<std::mutex> lk(cb_mu_);
        cb = cb_;
    }
    if (!cb) {
        __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
            "dispatch_message: cb_ is null");
        return;
    }

    Message m;
    if (!MessageParser::parse(raw, m)) {
        m.type = "raw";
        m.payload = raw;
    } else if (m.payload.empty()) {
        m.payload = raw;
    }
    m.data = bytes;
    m.timestamp = time_util::now_ms();

    __android_log_print(ANDROID_LOG_ERROR, "YAMGG-DBG",
        "dispatch_message: type=%s payload=%s",
        m.type.c_str(), m.payload.substr(0, 200).c_str());

    try { cb(m); }
    catch (const std::exception& e) { YAM_LOG_ERROR() << "msg cb: " << e.what(); }
    catch (...) { YAM_LOG_ERROR() << "msg cb: unknown"; }
}

// ===========================================================================
// SECTION 10 — Memory
// ===========================================================================

namespace {
int posix_prot_from(Protection p) {
    int r = 0;
    if (has_flag(p, Protection::Read))  r |= PROT_READ;
    if (has_flag(p, Protection::Write)) r |= PROT_WRITE;
    if (has_flag(p, Protection::Exec))  r |= PROT_EXEC;
    return r;
}
Protection protection_from_posix(int r) {
    u32 p = 0;
    if (r & PROT_READ)  p |= static_cast<u32>(Protection::Read);
    if (r & PROT_WRITE) p |= static_cast<u32>(Protection::Write);
    if (r & PROT_EXEC)  p |= static_cast<u32>(Protection::Exec);
    return static_cast<Protection>(p);
}
} // namespace
// ===========================================================================
// SECTION 11 — Allocation
// ===========================================================================

Allocation& Allocation::operator=(Allocation&& o) noexcept {
    if (this != &o) {
        if (ptr_) Memory::free(ptr_);
        ptr_ = o.ptr_; size_ = o.size_;
        o.ptr_ = nullptr; o.size_ = 0;
    }
    return *this;
}
void* Allocation::release() noexcept {
    auto* p = ptr_;
    ptr_ = nullptr; size_ = 0;
    return p;
}

// ===========================================================================
// SECTION 18 — RuntimeDiag
// ===========================================================================

namespace {
TimePoint g_start_time = Clock::now();
}

DiagSnapshot RuntimeDiag::snapshot() {
    DiagSnapshot s;
    s.runtime_init = Runtime::instance().is_initialized();
    s.java_ready = false; // set by JavaFacade
    s.registry_entries = registry().size();
    s.registry_bytes = registry().total_bytes();
    s.uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - g_start_time).count();
    return s;
}

String RuntimeDiag::to_json(const DiagSnapshot& s) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"runtime_init\":%s,\"java_ready\":%s,"
        "\"registry_entries\":%zu,\"registry_bytes\":%zu,"
        "\"java_hooks\":%zu,\"channels\":%zu,\"uptime_ms\":%lld}",
        s.runtime_init ? "true" : "false",
        s.java_ready ? "true" : "false",
        s.registry_entries, s.registry_bytes,
        s.java_hooks_count, s.channels_count,
        static_cast<long long>(s.uptime_ms));
    return buf;
}

String RuntimeDiag::to_string(const DiagSnapshot& s) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "runtime=%d java=%d reg=%zu bytes=%zu hooks=%zu chan=%zu up=%lldms",
        s.runtime_init, s.java_ready,
        s.registry_entries, s.registry_bytes,
        s.java_hooks_count, s.channels_count,
        static_cast<long long>(s.uptime_ms));
    return buf;
}

} // namespace yam

// ===========================================================================


// ===========================================================================
// APPENDIX — Definitions missing from the original DeepSeek-authored file.
// ===========================================================================

#include <sstream>
#include <fstream>
#include <cstring>

namespace yam {
namespace detail {

// ---------------------------------------------------------------------------
// ProcMaps::read — parse /proc/self/maps into a vector of entries.
// ---------------------------------------------------------------------------
std::vector<ProcMapsEntry> ProcMaps::read() {
    std::vector<ProcMapsEntry> out;
    std::ifstream f("/proc/self/maps");
    if (!f) return out;

    String line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        unsigned long long start = 0, end = 0;
        char perms[5] = {0};
        unsigned long long offset = 0;
        unsigned int dev_major = 0, dev_minor = 0;
        unsigned long inode = 0;

        int consumed = 0;
        int matched = std::sscanf(
            line.c_str(),
            "%llx-%llx %4s %llx %x:%x %lu %n",
            &start, &end, perms,
            &offset, &dev_major, &dev_minor, &inode,
            &consumed);

        if (matched < 4) continue;

        ProcMapsEntry e;
        e.start = static_cast<u64>(start);
        e.end   = static_cast<u64>(end);

        u32 p = 0;
        if (perms[0] == 'r') p |= static_cast<u32>(Protection::Read);
        if (perms[1] == 'w') p |= static_cast<u32>(Protection::Write);
        if (perms[2] == 'x') p |= static_cast<u32>(Protection::Exec);
        e.prot = static_cast<Protection>(p);

        if (consumed > 0 && static_cast<size_t>(consumed) < line.size()) {
            const char* rest = line.c_str() + consumed;
            while (*rest == ' ' || *rest == '\t') ++rest;
            e.path = rest;
        }

        out.push_back(std::move(e));
    }
    return out;
}

// ---------------------------------------------------------------------------
// HexPattern::compile — parse a hex pattern into a byte vector.
// ---------------------------------------------------------------------------
Result<ByteVector> HexPattern::compile(const String& pattern) {
    ByteVector out;
    out.reserve(pattern.size() / 2);

    size_t i = 0;
    const size_t n = pattern.size();
    while (i < n) {
        while (i < n && (std::isspace(static_cast<unsigned char>(pattern[i]))
                         || pattern[i] == ',')) {
            ++i;
        }
        if (i >= n) break;

        if (i + 1 < n && pattern[i] == '0'
            && (pattern[i+1] == 'x' || pattern[i+1] == 'X')) {
            i += 2;
        }
        if (i >= n) break;

        auto read_nibble = [](char c, int& v) -> bool {
            if (c >= '0' && c <= '9') { v = c - '0'; return true; }
            if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
            if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
            if (c == '?') { v = -1; return true; }
            return false;
        };

        int h0 = -1, h1 = -1;
        bool first_ok = false, second_ok = false;

        if (i < n) {
            first_ok = read_nibble(pattern[i], h0);
            if (first_ok) ++i;
        }
        if (i < n) {
            second_ok = read_nibble(pattern[i], h1);
            if (second_ok) ++i;
        }

        if (!first_ok && !second_ok) {
            return Result<ByteVector>::err(ErrorCode::InvalidArgument,
                String("bad hex pattern near: ") + pattern.substr(i, 4));
        }

        if (first_ok && !second_ok) {
            if (h0 < 0) out.push_back(0xFF);
            else out.push_back(static_cast<u8>(h0));
            continue;
        }

        u8 byte = 0;
        if (h0 < 0 && h1 < 0) {
            byte = 0xFF;
        } else if (h0 < 0 || h1 < 0) {
            byte = 0xFF;
        } else {
            byte = static_cast<u8>((h0 << 4) | h1);
        }
        out.push_back(byte);
    }

    if (out.empty()) {
        return Result<ByteVector>::err(ErrorCode::InvalidArgument,
            "empty hex pattern");
    }
    return Result<ByteVector>::ok(std::move(out));
}

} // namespace detail
// ===========================================================================
// SECTION 19 — ObjectRegistry
// ===========================================================================

ObjectRegistry& ObjectRegistry::instance() {
    static ObjectRegistry inst;
    return inst;
}

ObjectRegistry::ObjectRegistry() = default;
ObjectRegistry::~ObjectRegistry() { clear(); }

HandleId ObjectRegistry::register_entry(RegistryKind kind, const String& cls,
                                         void* native, void* extra) {
    std::lock_guard<std::mutex> lk(mu_);
    HandleId id = next_id_++;
    auto e = std::make_shared<RegistryEntry>();
    e->id = id;
    e->kind = kind;
    e->class_name = cls;
    e->native_handle = native;
    e->extra = extra;
    e->created_ms = time_util::now_ms();
    e->last_used_ms = e->created_ms;
    entries_[id] = e;
    total_bytes_.fetch_add(sizeof(RegistryEntry), std::memory_order_relaxed);
    return id;
}

Ptr<RegistryEntry> ObjectRegistry::lookup(HandleId id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = entries_.find(id);
    if (it == entries_.end()) return nullptr;
    it->second->last_used_ms = time_util::now_ms();
    return it->second;
}

bool ObjectRegistry::release(HandleId id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    entries_.erase(it);
    total_bytes_.fetch_sub(sizeof(RegistryEntry), std::memory_order_relaxed);
    return true;
}

void ObjectRegistry::retain(HandleId id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = entries_.find(id);
    if (it != entries_.end()) it->second->ref_count.fetch_add(1);
}

void ObjectRegistry::release_ref(HandleId id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = entries_.find(id);
    if (it == entries_.end()) return;
    u32 r = it->second->ref_count.fetch_sub(1);
    if (r <= 1) entries_.erase(it);
}

usize ObjectRegistry::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_.size();
}

usize ObjectRegistry::total_bytes() const {
    return total_bytes_.load(std::memory_order_relaxed);
}

void ObjectRegistry::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.clear();
    total_bytes_.store(0, std::memory_order_relaxed);
}

std::vector<Ptr<RegistryEntry>> ObjectRegistry::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Ptr<RegistryEntry>> out;
    out.reserve(entries_.size());
    for (auto& p : entries_) out.push_back(p.second);
    return out;
}

// ===========================================================================
// SECTION 20 — Gc
// ===========================================================================

void Gc::collect() {
    auto& r = registry();
    auto snap = r.snapshot();
    for (auto& e : snap) {
        if (!e) continue;
        if (e->ref_count.load(std::memory_order_acquire) == 0)
            r.release(e->id);
    }
}

void Gc::sweep(i64 max_age_ms) {
    auto& r = registry();
    i64 now = time_util::now_ms();
    auto snap = r.snapshot();
    for (auto& e : snap) {
        if (!e) continue;
        if (e->ref_count.load() == 0 && (now - e->last_used_ms) > max_age_ms)
            r.release(e->id);
    }
}

usize Gc::live_entries() { return registry().size(); }
usize Gc::live_bytes() { return registry().total_bytes(); }

} // namespace yam
