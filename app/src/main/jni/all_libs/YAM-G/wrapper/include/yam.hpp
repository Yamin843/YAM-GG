#ifndef YAM_HPP
#define YAM_HPP

// ===========================================================================
// YAM-CPP-WRAPPER — Single public header
// Full C++17 wrapper for libyamjs.a (frida-gum 17.22.x + embedded JS bridge).
// Target: Android arm64-v8a, NDK r25+, API 24+.
// ===========================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdarg>
#include <cerrno>
#include <ctime>

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <utility>
#include <optional>
#include <memory>
#include <functional>
#include <type_traits>
#include <variant>
#include <any>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <stdexcept>
#include <system_error>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <elf.h>

#if defined(__ANDROID__)
#  include <android/log.h>
#  include <jni.h>
#endif

// Real libyamjs headers.
extern "C" {
#include "YAMJS.h"
}
#include "gumpp.hpp"
#include "invocationcontext.hpp"
#include "invocationlistener.hpp"
#include "objectwrapper.hpp"
#include "podwrapper.hpp"
#include "runtime.hpp"
#include "string.hpp"

// -- Version ----------------------------------------------------------------
#define YAM_VERSION_MAJOR 1
#define YAM_VERSION_MINOR 0
#define YAM_VERSION_PATCH 0
#define YAM_VERSION_STRING "1.0.0"

#ifndef YAM_API
#  if defined(YAM_BUILD_SHARED)
#    define YAM_API __attribute__((visibility("default")))
#  else
#    define YAM_API
#  endif
#endif

#define YAM_NODISCARD  [[nodiscard]]
#define YAM_MAYBE_UNUSED [[maybe_unused]]
#define YAM_LIKELY(x)   __builtin_expect(!!(x), 1)
#define YAM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define YAM_NONCOPYABLE(C) C(const C&) = delete; C& operator=(const C&) = delete
#define YAM_NONMOVABLE(C)  C(C&&) = delete; C& operator=(C&&) = delete
#define YAM_DEFAULT_MOVABLE(C) C(C&&) noexcept = default; C& operator=(C&&) noexcept = default

namespace yam {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using isize = std::ptrdiff_t;

using byte = std::uint8_t;
using ByteVector = std::vector<byte>;
using String = std::string;
using StringView = std::string_view;

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

template <typename T> using Ptr = std::shared_ptr<T>;
template <typename T> using Unique = std::unique_ptr<T>;
template <typename T> using Ref = std::weak_ptr<T>;

// ===========================================================================
// SECTION 1 — Error
// ===========================================================================

enum class ErrorCode : int {
    Ok = 0, Unknown = -1, InvalidArgument = -2, OutOfRange = -3,
    NotInitialized = -4, AlreadyInitialized = -5, ScriptCompileFailed = -6,
    ScriptLoadFailed = -7, ScriptUnloadFailed = -8, BackendUnavailable = -9,
    Cancelled = -10, Timeout = -11, NotAttached = -12, AttachFailed = -13,
    DetachFailed = -14, ReplaceFailed = -15, RevertFailed = -16,
    MemoryAccessDenied = -17, MemoryReadFailed = -18, MemoryWriteFailed = -19,
    MemoryAllocFailed = -20, ModuleNotFound = -21, SymbolNotFound = -22,
    ClassNotFound = -23, MethodNotFound = -24, FieldNotFound = -25,
    OverloadAmbiguous = -26, OverloadNotFound = -27, JniNotAttached = -28,
    JniExceptionPending = -29, JavaVmNotFound = -30, TypeError = -31,
    NullDereference = -32, NotImplemented = -33, InternalError = -99,
    JsonParseError = -34, JsonTypeError = -35, ProtocolError = -36,
};

class ErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override;
    std::string message(int ev) const override;
};

YAM_API const std::error_category& error_category() noexcept;
YAM_API std::error_code make_error_code(ErrorCode c) noexcept;

class Error : public std::runtime_error {
public:
    Error(ErrorCode c, const String& m) : std::runtime_error(m), code_(c), msg_(m) {}
    Error(ErrorCode c, String&& m) : std::runtime_error(m), code_(c), msg_(std::move(m)) {}
    YAM_NODISCARD ErrorCode code() const noexcept { return code_; }
    YAM_NODISCARD const String& message() const noexcept { return msg_; }
private:
    ErrorCode code_;
    String msg_;
};

[[noreturn]] YAM_API void throw_error(ErrorCode c, const String& m);
[[noreturn]] YAM_API void throw_error(ErrorCode c, String&& m);

} // namespace yam

namespace std {
template <> struct is_error_code_enum<yam::ErrorCode> : true_type {};
}

namespace yam {

// ===========================================================================
// SECTION 2 — Result<T>
// ===========================================================================

template <typename T>
class Result {
public:
    Result() : has_(false) {}
    Result(const T& v) : has_(true), val_(v) {}
    Result(T&& v) : has_(true), val_(std::move(v)) {}
    Result(ErrorCode c, String m) : has_(false), err_(c), msg_(std::move(m)) {}
    Result(const Error& e) : has_(false), err_(e.code()), msg_(e.message()) {}

    static Result<T> ok(T v) { return Result<T>(std::move(v)); }
    static Result<T> err(ErrorCode c, String m = {}) { return Result<T>(c, std::move(m)); }

    YAM_NODISCARD bool has_value() const noexcept { return has_; }
    explicit operator bool() const noexcept { return has_; }

    T& value() { if (YAM_UNLIKELY(!has_)) throw Error(err_, msg_); return val_; }
    const T& value() const { if (YAM_UNLIKELY(!has_)) throw Error(err_, msg_); return val_; }
    T value_or(T f) const { return has_ ? val_ : std::move(f); }
    YAM_NODISCARD ErrorCode error_code() const noexcept { return err_; }
    YAM_NODISCARD const String& error_message() const noexcept { return msg_; }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }

private:
    bool has_;
    T val_{};
    ErrorCode err_{ErrorCode::Ok};
    String msg_;
};

template <>
class Result<void> {
public:
    Result() : ok_(true) {}
    Result(ErrorCode c, String m = {}) : ok_(c == ErrorCode::Ok), err_(c), msg_(std::move(m)) {}
    Result(const Error& e) : ok_(false), err_(e.code()), msg_(e.message()) {}
    static Result<void> ok() { return Result<void>(); }
    static Result<void> err(ErrorCode c, String m = {}) { return Result<void>(c, std::move(m)); }
    YAM_NODISCARD bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    void value() const { if (YAM_UNLIKELY(!ok_)) throw Error(err_, msg_); }
    YAM_NODISCARD ErrorCode error_code() const noexcept { return err_; }
    YAM_NODISCARD const String& error_message() const noexcept { return msg_; }
private:
    bool ok_{true};
    ErrorCode err_{ErrorCode::Ok};
    String msg_;
};

// ===========================================================================
// SECTION 3 — Log
// ===========================================================================

enum class LogLevel : int {
    Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5, Off = 6,
};

using LogCallback = std::function<void(LogLevel, const String&)>;

class Log {
public:
    static void set_level(LogLevel lv) noexcept;
    static LogLevel level() noexcept;
    static void set_callback(LogCallback cb);
    static void clear_callback();
    static void trace(const String& m);
    static void debug(const String& m);
    static void info(const String& m);
    static void warn(const String& m);
    static void error(const String& m);
    static void fatal(const String& m);
};

namespace detail {
class LogStream {
public:
    LogStream(LogLevel lv, const char* f, int l);
    ~LogStream();
    template <typename T> LogStream& operator<<(T&& v) {
        ss_ << std::forward<T>(v); return *this;
    }
private:
    LogLevel lv_;
    const char* file_;
    int line_;
    std::ostringstream ss_;
};
}

#define YAM_LOG_TRACE() ::yam::detail::LogStream(::yam::LogLevel::Trace, __FILE__, __LINE__)
#define YAM_LOG_DEBUG() ::yam::detail::LogStream(::yam::LogLevel::Debug, __FILE__, __LINE__)
#define YAM_LOG_INFO()  ::yam::detail::LogStream(::yam::LogLevel::Info,  __FILE__, __LINE__)
#define YAM_LOG_WARN()  ::yam::detail::LogStream(::yam::LogLevel::Warn,  __FILE__, __LINE__)
#define YAM_LOG_ERROR() ::yam::detail::LogStream(::yam::LogLevel::Error, __FILE__, __LINE__)
#define YAM_LOG_FATAL() ::yam::detail::LogStream(::yam::LogLevel::Fatal, __FILE__, __LINE__)

// ===========================================================================
// SECTION 4 — JSON (for bridge protocol)
// ===========================================================================

class JsonValue {
public:
    enum class Type : u8 { Null, Bool, Number, String, Array, Object };

    Type type{Type::Null};
    bool bool_val{false};
    f64  num_val{0};
    String str_val;
    std::vector<JsonValue> arr_val;
    std::unordered_map<String, JsonValue> obj_val;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : type(Type::Null) {}
    JsonValue(bool b) : type(Type::Bool), bool_val(b) {}
    JsonValue(i32 n) : type(Type::Number), num_val(n) {}
    JsonValue(i64 n) : type(Type::Number), num_val(static_cast<f64>(n)) {}
    JsonValue(u64 n) : type(Type::Number), num_val(static_cast<f64>(n)) {}
    JsonValue(f64 n) : type(Type::Number), num_val(n) {}
    JsonValue(const String& s) : type(Type::String), str_val(s) {}
    JsonValue(String&& s) : type(Type::String), str_val(std::move(s)) {}
    JsonValue(const char* s) : type(Type::String), str_val(s ? s : "") {}

    YAM_NODISCARD bool is_null() const noexcept { return type == Type::Null; }
    YAM_NODISCARD bool is_bool() const noexcept { return type == Type::Bool; }
    YAM_NODISCARD bool is_num()  const noexcept { return type == Type::Number; }
    YAM_NODISCARD bool is_str()  const noexcept { return type == Type::String; }
    YAM_NODISCARD bool is_arr()  const noexcept { return type == Type::Array; }
    YAM_NODISCARD bool is_obj()  const noexcept { return type == Type::Object; }

    YAM_NODISCARD bool as_bool(bool def = false) const noexcept {
        if (is_bool()) return bool_val;
        if (is_num()) return num_val != 0;
        return def;
    }
    YAM_NODISCARD i64 as_i64(i64 def = 0) const noexcept {
        if (is_num()) return static_cast<i64>(num_val);
        if (is_bool()) return bool_val ? 1 : 0;
        return def;
    }
    YAM_NODISCARD f64 as_f64(f64 def = 0) const noexcept {
        return is_num() ? num_val : def;
    }
    YAM_NODISCARD String as_str(const String& def = "") const {
        return is_str() ? str_val : def;
    }
    YAM_NODISCARD usize size() const noexcept {
        if (is_arr()) return arr_val.size();
        if (is_obj()) return obj_val.size();
        if (is_str()) return str_val.size();
        return 0;
    }

    const JsonValue* get(const String& k) const {
        if (!is_obj()) return nullptr;
        auto it = obj_val.find(k);
        return it == obj_val.end() ? nullptr : &it->second;
    }
    JsonValue* get(const String& k) {
        if (!is_obj()) return nullptr;
        auto it = obj_val.find(k);
        return it == obj_val.end() ? nullptr : &it->second;
    }

    void set(const String& k, JsonValue v) {
        if (!is_obj()) { type = Type::Object; obj_val.clear(); }
        obj_val[k] = std::move(v);
    }
    void push(JsonValue v) {
        if (!is_arr()) { type = Type::Array; arr_val.clear(); }
        arr_val.push_back(std::move(v));
    }

    YAM_NODISCARD String stringify() const;
    static Result<JsonValue> parse(const String& s);
    static Result<JsonValue> parse(StringView s);
};

// ===========================================================================
// SECTION 5 — String utilities
// ===========================================================================

namespace str {
bool starts_with(StringView s, StringView p) noexcept;
bool ends_with  (StringView s, StringView p) noexcept;
bool contains   (StringView s, StringView p) noexcept;
String to_lower(StringView s);
String to_upper(StringView s);
String trim(StringView s);
String trim_left(StringView s);
String trim_right(StringView s);
std::vector<String> split(StringView s, char delim);
std::vector<String> split(StringView s, StringView delim);
String join(const std::vector<String>& parts, StringView sep);
String replace_all(StringView s, StringView from, StringView to);
String format(const char* fmt, ...);
String vformat(const char* fmt, va_list ap);
String hex(u64 v, bool prefix = false, int width = 0);
String hex(const void* p, bool prefix = true);
bool parse_i64(StringView s, i64& out, int base = 10) noexcept;
bool parse_u64(StringView s, u64& out, int base = 10) noexcept;
bool parse_f64(StringView s, f64& out) noexcept;
String jni_to_dotted(StringView jni);
String dotted_to_jni(StringView dotted);
String dotted_to_path(StringView dotted);
String escape_json(StringView s);
String unescape_json(StringView s);
} // namespace str

namespace time_util {
i64 now_ms() noexcept;
i64 now_us() noexcept;
i64 now_ns() noexcept;
String iso8601(i64 ms);
} // namespace time_util

// ===========================================================================
// SECTION 6 — ThreadPool
// ===========================================================================

class ThreadPool {
public:
    explicit ThreadPool(usize n = 0);
    ~ThreadPool();
    YAM_NONCOPYABLE(ThreadPool);
    template <typename Fn, typename... Args>
    auto submit(Fn&& fn, Args&&... args)
        -> std::future<std::invoke_result_t<Fn, Args...>>;
    void wait_all();
    usize size() const noexcept { return threads_.size(); }
    void stop();
private:
    void worker_loop();
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> queue_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;
    bool stop_{false};
    usize active_{0};
};

template <typename Fn, typename... Args>
auto ThreadPool::submit(Fn&& fn, Args&&... args)
    -> std::future<std::invoke_result_t<Fn, Args...>>
{
    using R = std::invoke_result_t<Fn, Args...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
        [fn = std::forward<Fn>(fn),
         tup = std::make_tuple(std::forward<Args>(args)...)]() mutable -> R {
            return std::apply(fn, std::move(tup));
        });
    auto fut = task->get_future();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) throw Error(ErrorCode::Cancelled, "pool stopped");
        queue_.emplace([task]{ (*task)(); });
        ++active_;
    }
    cv_.notify_one();
    return fut;
}

// ===========================================================================
// SECTION 7 — Runtime / Script
// ===========================================================================

enum class BackendKind : int { QJS = 0, V8 = 1 };

struct RuntimeOptions {
    bool  enable_debugger{false};
    usize worker_threads{0};
    LogLevel log_level{LogLevel::Info};
    String script_name{"yam"};
    BackendKind backend{BackendKind::QJS};
};

class Runtime {
public:
    static Runtime& instance();
    Result<void> init();
    Result<void> init(const RuntimeOptions& opt);
    Result<void> shutdown();
    YAM_NODISCARD bool is_initialized() const noexcept { return initialized_; }
    YAM_NODISCARD const RuntimeOptions& options() const noexcept { return opts_; }
    YAM_NODISCARD void* backend_handle() const noexcept { return backend_; }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
private:
    Runtime();
    ~Runtime();
    bool initialized_{false};
    RuntimeOptions opts_;
    void* backend_{nullptr};
    mutable std::mutex mu_;
};
inline Runtime& runtime() { return Runtime::instance(); }

struct Message {
    String      type;
    String      payload;
    ByteVector  data;
    i64         timestamp{0};
};

using MessageCallback = std::function<void(const Message&)>;

class MessageParser {
public:
    static bool parse(const String& raw, Message& out);
};

class Cancellable {
public:
    Cancellable();
    ~Cancellable();
    YAM_NONCOPYABLE(Cancellable);
    void cancel();
    YAM_NODISCARD bool is_cancelled() const noexcept;
    void* native_handle() const noexcept { return handle_; }
private:
    void* handle_{nullptr};
};

class Script : public std::enable_shared_from_this<Script> {
public:
    static Ptr<Script> create(const String& name, const String& source);
    static Ptr<Script> create(const String& name, const String& source,
                              const ByteVector& bytes);
    ~Script();
    YAM_NONCOPYABLE(Script);
    YAM_NODISCARD const String& name() const noexcept { return name_; }
    YAM_NODISCARD void* native_handle() const noexcept { return handle_; }
    YAM_NODISCARD bool is_loaded() const noexcept { return loaded_; }
    Result<void> load();
    Result<void> load(Cancellable& c);
    Result<void> unload();
    Result<void> unload(Cancellable& c);
    void set_message_handler(MessageCallback cb);
    void clear_message_handler();
    void post(const String& message);
    void post(const String& message, const ByteVector& data);
    void dispatch_message(const String& raw, const ByteVector& bytes);
private:
    Script(const String& name, const String& source, const ByteVector& bytes);
    String name_;
    String source_;
    ByteVector source_bytes_;
    void* handle_{nullptr};
    bool  loaded_{false};
    MessageCallback cb_;
    mutable std::mutex cb_mu_;
};

// ===========================================================================
// SECTION 8 — InvocationContext / CpuContext
// ===========================================================================

class CpuContext {
public:
    explicit CpuContext(void* raw) : raw_(raw) {}
    void* raw() const noexcept { return raw_; }
    bool valid() const noexcept { return raw_ != nullptr; }
    u64 pc() const noexcept;
    u64 sp() const noexcept;
    u64 lr() const noexcept;
    u64 fp() const noexcept;
    u64 reg(const char* name) const noexcept;
    void set_reg(const char* name, u64 v) noexcept;
private:
    void* raw_{nullptr};
};

class InvocationContext {
public:
    explicit InvocationContext(void* raw) : raw_(raw) {}
    void* raw() const noexcept { return raw_; }
    template <typename T = void*> T argument(unsigned n) const {
        return static_cast<T>(arg_ptr(n));
    }
    void* arg_ptr(unsigned n) const;
    void  set_arg_ptr(unsigned n, void* v);
    template <typename T = void*> T return_value() const {
        return static_cast<T>(ret_ptr());
    }
    void* ret_ptr() const;
    unsigned thread_id() const;
    void* listener_thread_data(usize sz) const;
    void* listener_function_data() const;
    void* listener_invocation_data(usize sz) const;
    void* replacement_data() const;
    Ptr<CpuContext> cpu_context() const;
    i32 arg_i32(unsigned n) const;
    u32 arg_u32(unsigned n) const;
    i64 arg_i64(unsigned n) const;
    u64 arg_u64(unsigned n) const;
    f32 arg_f32(unsigned n) const;
    f64 arg_f64(unsigned n) const;
    String arg_string(unsigned n) const;
    String arg_utf16(unsigned n) const;
private:
    void* raw_{nullptr};
};

using InvocationHook = std::function<void(InvocationContext&)>;

// ===========================================================================
// SECTION 9 — Native interceptor
// ===========================================================================

class Attachment {
public:
    Attachment() = default;
    ~Attachment();
    YAM_NONCOPYABLE(Attachment);
    YAM_DEFAULT_MOVABLE(Attachment);
    YAM_NODISCARD bool valid() const noexcept { return listener_ != nullptr; }
    void detach();
    YAM_NODISCARD void* id() const noexcept { return listener_; }
private:
    friend class Interceptor;
    Attachment(void* l, std::function<void()> fn);
    void* listener_{nullptr};
    std::function<void()> detach_fn_;
};

class Interceptor {
public:
    static Interceptor& instance();
    YAM_NONCOPYABLE(Interceptor);
    Attachment attach(void* target, InvocationHook on_enter,
                      InvocationHook on_leave = nullptr);
    Attachment attach(const String& symbol, InvocationHook on_enter,
                      InvocationHook on_leave = nullptr);
    Result<void> detach(Attachment& att);
    Result<void> replace(void* target, void* replacement, void* data = nullptr);
    Result<void> revert(void* target);
    Result<void> attach_probe(void* target, InvocationHook on_hit);
    Result<void> detach_probe(void* target);
    void begin_transaction();
    void end_transaction();
    Ptr<InvocationContext> current_invocation();
    void ignore_current_thread();
    void unignore_current_thread();
    void ignore_other_threads();
    void unignore_other_threads();
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    YAM_NODISCARD void* raw() const noexcept { return raw_; }
private:
    Interceptor();
    ~Interceptor();
    void* raw_{nullptr};
};
inline Interceptor& interceptor() { return Interceptor::instance(); }

// ===========================================================================
// SECTION 10 — Backtracer
// ===========================================================================

struct BacktraceFrame {
    void*  address{nullptr};
    String module_name;
    String function_name;
    String file_name;
    u32    line{0};
    u32    column{0};
};

class Backtracer {
public:
    enum class Mode { Accurate, Fuzzy };
    Backtracer();
    explicit Backtracer(Mode m);
    ~Backtracer();
    YAM_NONCOPYABLE(Backtracer);
    YAM_DEFAULT_MOVABLE(Backtracer);
    YAM_NODISCARD bool valid() const noexcept { return bt_ != nullptr; }
    Result<std::vector<void*>> generate(const CpuContext& ctx) const;
    static Result<BacktraceFrame> details(void* address);
    YamBacktracer* raw() const noexcept { return bt_; }
private:
    YamBacktracer* bt_{nullptr};
};

// ===========================================================================
// SECTION 11 — Memory
// ===========================================================================

enum class Protection : u32 {
    None = 0, Read = 1, Write = 2, Exec = 4,
    RW = Read | Write, RX = Read | Exec, RWX = Read | Write | Exec,
};
inline Protection operator|(Protection a, Protection b) noexcept {
    return static_cast<Protection>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline bool has_flag(Protection p, Protection f) noexcept {
    return (static_cast<u32>(p) & static_cast<u32>(f)) == static_cast<u32>(f);
}

class Memory {
public:
    static Result<void> protect(void* addr, usize size, Protection prot);
    static Result<Protection> query(void* addr);
    static Result<void*> alloc(usize size);
    static Result<void*> alloc(usize size, Protection prot);
    static Result<void*> alloc_near(void* near, usize size);
    static Result<void>  free(void* addr);
    static Result<void> copy(void* dst, const void* src, usize size);
    static Result<void> read(void* addr, void* out, usize size);
    static Result<void> write(void* addr, const void* in, usize size);
    static Result<ByteVector> read_bytes(void* addr, usize size);
    static Result<void>       write_bytes(void* addr, const ByteVector& d);
    static Result<u8>  read_u8(void* a);
    static Result<u16> read_u16(void* a);
    static Result<u32> read_u32(void* a);
    static Result<u64> read_u64(void* a);
    static Result<i8>  read_i8(void* a);
    static Result<i16> read_i16(void* a);
    static Result<i32> read_i32(void* a);
    static Result<i64> read_i64(void* a);
    static Result<f32> read_f32(void* a);
    static Result<f64> read_f64(void* a);
    static Result<void*> read_ptr(void* a);
    static Result<void> write_u8(void* a, u8 v);
    static Result<void> write_u16(void* a, u16 v);
    static Result<void> write_u32(void* a, u32 v);
    static Result<void> write_u64(void* a, u64 v);
    static Result<void> write_i8(void* a, i8 v);
    static Result<void> write_i16(void* a, i16 v);
    static Result<void> write_i32(void* a, i32 v);
    static Result<void> write_i64(void* a, i64 v);
    static Result<void> write_f32(void* a, f32 v);
    static Result<void> write_f64(void* a, f64 v);
    static Result<void> write_ptr(void* a, void* v);
    static String read_cstring(void* addr, usize max = 4096);
    static String read_utf8(void* addr, usize size);
    static String read_utf16(void* addr, usize chars);
    static Result<void> write_utf8(void* addr, const String& s);
    static Result<std::vector<void*>> scan(void* base, usize size, const String& pattern);
    static Result<void*> scan_first(void* base, usize size, const String& pattern);
    static Result<std::vector<void*>> scan_module(const String& module, const String& pattern);
    static bool is_readable(void* addr, usize size = 1);
    static bool is_writable(void* addr, usize size = 1);
    static bool is_executable(void* addr, usize size = 1);
};

// ===========================================================================
// Allocation — RAII wrapper over Memory::alloc / Memory::free
// ===========================================================================

class Allocation {
public:
    Allocation() = default;
    explicit Allocation(void* p, usize s) : ptr_(p), size_(s) {}
    ~Allocation() { if (ptr_) Memory::free(ptr_); }
    YAM_NONCOPYABLE(Allocation);
    Allocation(Allocation&& o) noexcept : ptr_(o.ptr_), size_(o.size_) {
        o.ptr_ = nullptr;
        o.size_ = 0;
    }
    Allocation& operator=(Allocation&& o) noexcept;
    void* release() noexcept;
    YAM_NODISCARD void* ptr() const noexcept { return ptr_; }
    YAM_NODISCARD usize size() const noexcept { return size_; }
    YAM_NODISCARD bool valid() const noexcept { return ptr_ != nullptr; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
private:
    void* ptr_{nullptr};
    usize size_{0};
};

// ===========================================================================
// SECTION 12 — Module / Symbol / Range
// ===========================================================================

struct ExportSymbol { String name; void* address{nullptr}; String type; };
struct ImportSymbol { String name; String module; void* address{nullptr}; };
struct ModuleRange {
    void* base{nullptr};
    usize size{0};
    Protection protection{Protection::None};
    String file;
};

class Module {
public:
    Module() = default;
    YAM_NODISCARD const String& name() const noexcept { return name_; }
    YAM_NODISCARD const String& path() const noexcept { return path_; }
    YAM_NODISCARD void*  base() const noexcept { return base_; }
    YAM_NODISCARD usize  size() const noexcept { return size_; }
    YAM_NODISCARD bool   valid() const noexcept { return base_ != nullptr; }
    YAM_NODISCARD bool contains(void* addr) const noexcept {
        auto b = reinterpret_cast<uintptr_t>(base_);
        auto a = reinterpret_cast<uintptr_t>(addr);
        return a >= b && a < b + size_;
    }
    std::vector<ExportSymbol> exports() const;
    std::vector<ImportSymbol> imports() const;
    std::vector<ModuleRange>  ranges() const;
    void* find_export(const String& name) const;
    void* find_symbol(const String& name) const;
    static Result<Module> find(const String& name);
    static Result<Module> find_by_address(void* addr);
    static std::vector<Module> enumerate();
    static Module main();
    static void* find_global_export(const String& name);
private:
    static gboolean visit_module(YamModule* m, gpointer user);
    String name_;
    String path_;
    void*  base_{nullptr};
    usize  size_{0};
};

class Symbol {
public:
    static void* resolve(const String& name);
    static void* resolve_in(const String& module, const String& name);
    static std::vector<void*> resolve_matching(const String& pattern);
    static String demangle(const String& mangled);
    static String to_string(void* addr);
};

// ===========================================================================
// SECTION 13 — MemoryPatch
// ===========================================================================

class MemoryPatch {
public:
    MemoryPatch();
    ~MemoryPatch();
    YAM_NONCOPYABLE(MemoryPatch);
    Result<void> install(void* addr, const ByteVector& code);
    Result<void> install(void* addr, const String& hex);
    Result<void> uninstall();
    YAM_NODISCARD bool installed() const noexcept { return installed_; }
private:
    void* addr_{nullptr};
    ByteVector original_;
    bool installed_{false};
};

YAM_API ByteVector parse_hex_code(const String& hex);

// ===========================================================================
// SECTION 14 — Handle / Registry
// ===========================================================================

using HandleId = u64;
constexpr HandleId kInvalidHandle = 0;

enum class RegistryKind : int {
    JavaObject = 0, JavaClass = 1, JavaMethod = 2, JavaField = 3,
    NativeObject = 4, NativeHandle = 5, GumObject = 6,
    Callback = 7, Channel = 8, BridgeHandle = 9,
};

struct RegistryEntry {
    HandleId     id{kInvalidHandle};
    RegistryKind kind{RegistryKind::JavaObject};
    String       class_name;
    void*        native_handle{nullptr};
    void*        extra{nullptr};
    i64          created_ms{0};
    i64          last_used_ms{0};
    std::atomic<u32> ref_count{1};
    bool         owned{true};
    YAM_NODISCARD bool alive() const noexcept {
        return ref_count.load(std::memory_order_acquire) > 0;
    }
};

class ObjectRegistry {
public:
    static ObjectRegistry& instance();
    HandleId register_entry(RegistryKind kind, const String& cls,
                            void* native, void* extra = nullptr);
    Ptr<RegistryEntry> lookup(HandleId id) const;
    bool release(HandleId id);
    void retain(HandleId id);
    void release_ref(HandleId id);
    YAM_NODISCARD usize size() const;
    YAM_NODISCARD usize total_bytes() const;
    void clear();
    std::vector<Ptr<RegistryEntry>> snapshot() const;
private:
    ObjectRegistry();
    ~ObjectRegistry();
    void destroy_entry(RegistryEntry& e);
    mutable std::mutex mu_;
    std::unordered_map<HandleId, Ptr<RegistryEntry>> entries_;
    HandleId next_id_{1};
    std::atomic<usize> total_bytes_{0};
};
inline ObjectRegistry& registry() { return ObjectRegistry::instance(); }

class Handle {
public:
    Handle() = default;
    explicit Handle(HandleId id) : id_(id) {}
    YAM_NODISCARD HandleId id() const noexcept { return id_; }
    YAM_NODISCARD bool valid() const noexcept { return id_ != kInvalidHandle; }
    Ptr<RegistryEntry> entry() const;
    String class_name() const;
    RegistryKind kind() const;
    void* raw() const;
    void retain() const;
    void release() const;
private:
    HandleId id_{kInvalidHandle};
};

class Gc {
public:
    static void collect();
    static void sweep(i64 max_age_ms = 30000);
    static usize live_entries();
    static usize live_bytes();
};

// ===========================================================================
// SECTION 15 — Value
// ===========================================================================

class Value {
public:
    enum class Kind { Null, Undefined, Bool, Int, Long, Float, Double, String, Object, Array, Class, Void };
    Value() : kind_(Kind::Null) {}
    Value(std::nullptr_t) : kind_(Kind::Null) {}
    Value(bool b) : kind_(Kind::Bool), b_(b) {}
    Value(i32 v) : kind_(Kind::Int), i_(v) {}
    Value(u32 v) : kind_(Kind::Long), l_(static_cast<i64>(v)) {}
    Value(i64 v) : kind_(Kind::Long), l_(v) {}
    Value(u64 v) : kind_(Kind::Long), l_(static_cast<i64>(v)) {}
    Value(f32 v) : kind_(Kind::Float), f_(v) {}
    Value(f64 v) : kind_(Kind::Double), d_(v) {}
    Value(const String& s) : kind_(Kind::String), s_(s) {}
    Value(String&& s) : kind_(Kind::String), s_(std::move(s)) {}
    Value(const char* s) : kind_(Kind::String), s_(s ? s : "") {}
    YAM_NODISCARD Kind kind() const noexcept { return kind_; }
    YAM_NODISCARD bool is_null() const noexcept { return kind_ == Kind::Null; }
    YAM_NODISCARD bool as_bool() const { return b_; }
    YAM_NODISCARD i32 as_int() const { return kind_ == Kind::Long ? static_cast<i32>(l_) : i_; }
    YAM_NODISCARD i64 as_long() const { return kind_ == Kind::Int ? static_cast<i64>(i_) : l_; }
    YAM_NODISCARD f32 as_float() const { return kind_ == Kind::Double ? static_cast<f32>(d_) : f_; }
    YAM_NODISCARD f64 as_double() const { return kind_ == Kind::Float ? static_cast<f64>(f_) : d_; }
    YAM_NODISCARD String as_string() const;
private:
    Kind kind_;
    union { bool b_; i32 i_; i64 l_; f32 f_; f64 d_; };
    String s_;
};

// ===========================================================================
// SECTION 16 — JNI
// ===========================================================================

namespace jni {
using jboolean = u8;
using jbyte    = i8;
using jchar    = u16;
using jshort   = i16;
using jint     = i32;
using jlong    = i64;
using jfloat   = f32;
using jdouble  = f64;
using jsize    = i32;
using jobject  = void*;
using jclass   = jobject;
using jstring  = jobject;
using jarray   = jobject;
using jmethodID = void*;
using jfieldID  = void*;
struct JNIEnv;
struct JavaVM;
}

class JniSignature {
public:
    String return_jni;
    String return_dotted;
    std::vector<String> args_jni;
    std::vector<String> args_dotted;
    bool valid{false};
    String to_string() const;
};

class JniSignatureParser {
public:
    static JniSignature parse(const String& sig);
    static String to_jni(const String& dotted);
    static String to_dotted(const String& jni);
    static bool is_primitive(const String& jni);
};

// ===========================================================================
// SECTION 17 — Java reply
// ===========================================================================

struct JavaReply {
    u64    id{0};
    bool   ok{false};
    String error;
    String kind;      // handle|value|null|void|error|pong|array
    String result;    // when kind==value (may be JSON stringified)
    u64    handle{0}; // when kind==handle
};

class JavaReplyParser {
public:
    static bool parse(const String& json, JavaReply& out);
};

// ===========================================================================
namespace detail {

class JavaSync {
public:
    JavaSync() = default;
    YAM_NONCOPYABLE(JavaSync);

    void wait(i64 timeout_ms = 5000) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                     [this]{ return ready_; });
    }
    void notify() {
        std::lock_guard<std::mutex> lk(mu_);
        ready_ = true;
        cv_.notify_all();
    }
    YAM_NODISCARD bool ready() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return ready_;
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool ready_{false};
};

} // namespace detail

// SECTION 18 — JavaScriptBridge (protocol correct)
// ===========================================================================

class JavaScriptBridge {
public:
    static JavaScriptBridge& instance();

    Result<void> initialize();
    Result<void> shutdown();
    YAM_NODISCARD bool is_ready() const noexcept { return ready_.load(); }

    // Send a command without waiting. Results come as events.
    Result<void> send(const String& command_json);

    // Send a custom command (cpp_*) and wait for a reply envelope.
    Result<JavaReply> call(const String& command_json, i64 timeout_ms = 10000);

    // Post a message on a named channel (for bp_reply_<id>).
    void post_raw(const String& type, const JsonValue& payload);
    void post_raw_json(const String& json);

    // Access underlying script.
    Ptr<Script> script() const { return script_; }

    // Custom commands (registered by our bootstrap)
    Result<JavaReply> use_class(const String& cls);
    Result<JavaReply> get_method(u64 cls, const String& m, const String& sig);
    Result<JavaReply> cast(u64 cls, u64 obj);
    Result<JavaReply> create_string(const String& v);
    Result<JavaReply> array_of(const String& t, const String& e);
    Result<JavaReply> array_length(u64 h);
    Result<JavaReply> array_get(u64 h, usize i);
    Result<JavaReply> array_set(u64 h, usize i, const String& v);
    Result<JavaReply> new_instance(u64 cls, const String& args);
    Result<JavaReply> call_method(u64 inst, u64 m, const String& args);
    Result<JavaReply> get_field(u64 inst, u64 f);
    Result<void> set_field(u64 inst, u64 f, const String& v);
    Result<JavaReply> release_handle(u64 h);
    Result<JavaReply> list_handles();
    Result<JavaReply> clear_handles();
    Result<JavaReply> inspect_handle(u64 h);
    Result<JavaReply> get_field_by_name(const String& cls, const String& f);
    Result<JavaReply> snapshot_fields(u64 h);
    Result<JavaReply> retain(u64 h);

    // Native bridge commands (fire-and-forget; events come back)
    Result<JavaReply> ping();
    Result<void> trace(u64 tid, const String& c, const String& m,
                        const String& s, i32 st, i32 sk, i32 mm,
                        bool has_ov, const String& ov);
    Result<void> trace_class(u64 tid, const String& c, const String& p,
                              i32 st, i32 sk, i32 mm);
    Result<void> untrace(u64 tid);
    Result<void> untrace_range(u64 first, u64 last);
    Result<void> clear_traces();
    Result<void> pause(u64 tid);
    Result<void> resume(u64 tid);
    Result<void> set_param(u64 tid, i32 i, const String& t, const String& v);
    Result<void> clear_param(u64 tid, i32 i);
    Result<void> set_param_spec(u64 tid, i32 i, const String& s);
    Result<JavaReply> enumerate_classes();
    Result<JavaReply> enumerate_loaders();
    Result<void> enumerate_methods(const String& q);
    Result<JavaReply> backtrace(i32 l);
    Result<void> list_targets();
    Result<JavaReply> env_info();
    Result<void> hook_dex_loader();
    Result<void> hook_load_class();
    Result<void> hook_load_library();
    Result<void> hook_app_oncreate();
    Result<void> deopt_class(const String& c);
    Result<void> deopt_everything();
    Result<void> deopt_boot_image();
    Result<void> deopt_method(u64 h);
    Result<void> list_hooks();
    Result<JavaReply> get_internals();
    Result<JavaReply> list_own(const String& c);
    Result<JavaReply> list_overloads(const String& c, const String& m);
    Result<void> query_methods(const String& q);
    Result<void> build_template(const String& c, const String& m, const String& s);
    Result<JavaReply> probe_class(const String& c);
    Result<void> inspect_class(u64 h);
    Result<void> read_field(u64 h, const String& f);
    Result<void> write_field(u64 h, const String& f, const String& e);
    Result<JavaReply> read_static(const String& c, const String& f);
    Result<void> write_static(const String& c, const String& f, const String& e);
    Result<JavaReply> list_static_fields(const String& c);
    Result<void> call(u64 call_id, const String& cls, const String& m,
                       const String& sig, bool is_static, u64 inst,
                       const String& args_json, const String& mod_fields = "");
    Result<void> construct(u64 call_id, const String& cls,
                            const String& ctor_sig, const String& args);
    Result<void> read_path(u64 h, const String& p);
    Result<void> write_path(u64 h, const String& p, const String& e);
    Result<void> invoke_on(u64 h, const String& m, const String& sig,
                            const String& args);
    Result<void> hook_method(u64 m, i64 cb);
    Result<void> unhook_method(u64 m);
    Result<JavaReply> choose(const String& c);
    Result<void> set_breakpoint(u64 tid, bool e, i32 every);
    Result<void> run_user_script(const String& n, const String& c);
    Result<void> load_scripts_batch(const std::vector<std::pair<String,String>>& s);
    Result<void> list_user_scripts();
    Result<void> clear_user_scripts();
    Result<void> watch_start(u64 h, const String& f, i64 ms);
    Result<void> watch_stop(u64 wid);
    Result<void> watch_stop_all();
    Result<void> watch_list();
    Result<void> watch_start_interval(u64 h, const String& f, i32 ms);
    Result<void> bp_state();
    Result<void> cancel_all_breakpoints();
    Result<void> set_bp_timeout(i64 ms);
    Result<void> set_throwable_stacktrace(bool e);
    Result<void> clear_probe_cache();
    Result<void> set_repl_async_enabled(bool e);
    Result<void> agent_emit(const String& ev, const String& payload);
    Result<JavaReply> list_extensions();
    Result<JavaReply> get_handle_budget();
    Result<void> handle_bytes();
    Result<void> repl_history();
    Result<void> repl_history_clear();
    Result<void> call_history();
    Result<void> call_history_clear();
    Result<void> console_dump();
    Result<void> console_clear();
    Result<void> hook_native(const String& a, const String& e, const String& l);
    Result<void> replace_native(const String& a, const String& b);
    Result<void> unhook_native(u64 hid);
    Result<void> revert_native();
    Result<void> list_native();
    Result<void> scan_memory(const String& a, u64 s, const String& p);
    Result<void> modules_list(const String& f);
    Result<void> exports_list(const String& m, const String& f);
    Result<void> auto_add(const String& c, const String& p);
    Result<void> auto_remove(u64 id);
    Result<void> auto_list();
    Result<void> auto_clear();
    Result<void> auto_enable(bool e);
    Result<void> update_auto_rule(u64 tid, const String& p);
    Result<void> chunk_begin(const String& kind, const String& payload);
    Result<void> chunk_next(u64 sid, u64 idx);
    Result<void> chunk_all(u64 sid);
    Result<void> chunk_end(u64 sid);
    Result<void> chunk_list();
    Result<void> trace_handles_list(u64 tid);
    Result<void> trace_handles_clear();
    Result<void> replay(u64 rid, const String& cls, const String& m,
                         const String& sig, bool is_static, u64 inst,
                         const String& args, i32 times = 1,
                         i32 interleave_ms = 0, bool stop_on_error = false);
    Result<JavaReply> eval(const String& c);

    Result<void> wait_cpp_ready(i64 timeout_ms = 15000);

    // phase: "enter" | "leave" | "exception"
    // ret_h: valid only for "leave" (0 = void or unknown)
    // is_void: true when original returned void
    // ex_msg: valid only for "exception"
    // phase: "enter" | "leave" | "exception"
    // ret_h: valid only for "leave" (0 = void / unknown)
    // is_void: true when original returned void
    // ex_msg: valid only for "exception"
    using HookCallback = std::function<void(
        i64 /*cbId*/, const String& /*phase*/,
        const std::vector<u64>& /*args*/, u64 /*thisH*/,
        u64 /*retH*/, bool /*isVoid*/, const String& /*exMsg*/)>;
    using ConsoleCallback = std::function<void(const String&, const String&)>;
    using EvalCallback = std::function<void(u64, bool, const String&, const String&)>;
    void set_hook_callback(HookCallback cb);
    void set_console_callback(ConsoleCallback cb);
    void set_eval_callback(EvalCallback cb);

private:
    JavaScriptBridge();
    ~JavaScriptBridge();
    void on_message(const Message& m);
    void on_reply(const String& payload_json);
    void on_event_json(const JsonValue& ev);

    Ptr<Script> script_;
    std::atomic<bool> ready_{false};
    std::atomic<u64> next_id_{1};
    std::mutex pending_mu_;
    std::unordered_map<u64, Ptr<detail::JavaSync>> pending_;
    std::unordered_map<u64, JavaReply> replies_;
    HookCallback hook_cb_;
    ConsoleCallback console_cb_;
    EvalCallback eval_cb_;
    std::mutex cb_mu_;
};

inline JavaScriptBridge& java_bridge() { return JavaScriptBridge::instance(); }

// ===========================================================================
// SECTION 19 — Java facade (high-level)
// ===========================================================================

class JavaFacade {
public:
    static Result<void> start();
    static Result<void> stop();
    YAM_NODISCARD static bool is_ready();
    static Result<Handle> use(const String& cls);
    static Result<std::vector<String>> enumerate_classes();
    static Result<std::vector<String>> enumerate_loaders();
    static Result<u64> method(u64 cls_handle, const String& n, const String& sig);
    static Result<u64> new_instance(u64 cls, const std::vector<u64>& args);
    static Result<u64> cast(u64 cls, u64 obj);
    static Result<JavaReply> call(u64 inst, u64 method, const std::vector<u64>& args);
    static Result<JavaReply> call_static(u64 cls, u64 method, const std::vector<u64>& args);
    static Result<JavaReply> get_field(u64 inst, u64 field);
    static Result<void> set_field(u64 inst, u64 field, u64 value);
    static Result<u64> create_string(const String& utf8);
    static Result<void> deopt_method(u64 method);
    static Result<void> deopt_class(const String& cls);
    static Result<void> deopt_everything();
    static Result<void> deopt_boot_image();
    static Result<void> hook_method(u64 method,
                                    std::function<void(const std::vector<u64>&, u64)> cb);
    static Result<void> unhook_method(u64 method);
    static Result<usize> choose(const String& cls,
                                std::function<int(Handle)> on_match);
    static Result<std::vector<Handle>> choose_all(const String& cls);
};

class JavaHookManager {
public:
    using Fn = std::function<void(const std::vector<u64>& args, u64 this_handle)>;
    static JavaHookManager& instance();
    Result<HandleId> hook(const String& cls, const String& method,
                          const String& sig, Fn fn);
    Result<void> unhook(HandleId id);
    void clear();
    usize size() const;
private:
    JavaHookManager();
    ~JavaHookManager();
    mutable std::mutex mu_;
    // HandleId -> cb_id (the id used to key g_hook_fns in yam_java.cpp).
    // The actual Fn lives in g_hook_fns (shared with the bridge callback).
    // Storing only cb_id lets unhook() clean BOTH tables — the old map
    // of <HandleId, Fn> leaked one entry per destroyed hook.
    std::unordered_map<HandleId, std::int64_t> hooks_;
};
inline JavaHookManager& java_hooks() { return JavaHookManager::instance(); }

// ===========================================================================
// SECTION 20 — GumCxx
// ===========================================================================

class GumCxx {
public:
    static Yam::Interceptor* interceptor();
    static Yam::Backtracer* backtracer_accurate();
    static Yam::Backtracer* backtracer_fuzzy();
    static void* find_function(const String& name);
    static std::vector<void*> find_matching(const String& pattern);
    static bool details_from_address(void* addr, Yam::ReturnAddressDetails& out);
};

class GumObjRef {
public:
    GumObjRef() = default;
    explicit GumObjRef(Yam::Object* o);
    GumObjRef(const GumObjRef& o);
    GumObjRef(GumObjRef&& o) noexcept;
    GumObjRef& operator=(const GumObjRef& o);
    GumObjRef& operator=(GumObjRef&& o) noexcept;
    ~GumObjRef();
    Yam::Object* get() const noexcept { return obj_; }
    bool valid() const noexcept { return obj_ != nullptr; }
    void reset();
    Yam::Object* release() noexcept;
private:
    Yam::Object* obj_{nullptr};
};

class GumStr {
public:
    GumStr() = default;
    explicit GumStr(Yam::String* s);
    ~GumStr();
    YAM_NONCOPYABLE(GumStr);
    YAM_DEFAULT_MOVABLE(GumStr);
    YAM_NODISCARD Yam::String* raw() const noexcept { return str_; }
    YAM_NODISCARD bool valid() const noexcept { return str_ != nullptr; }
    YAM_NODISCARD const char* c_str() const;
    YAM_NODISCARD usize length() const;
    YAM_NODISCARD String str() const;
private:
    Yam::String* str_{nullptr};
};

class GumArray {
public:
    GumArray() = default;
    explicit GumArray(Yam::PtrArray* a);
    ~GumArray();
    YAM_NONCOPYABLE(GumArray);
    YAM_DEFAULT_MOVABLE(GumArray);
    YAM_NODISCARD Yam::PtrArray* raw() const noexcept { return arr_; }
    YAM_NODISCARD bool valid() const noexcept { return arr_ != nullptr; }
    YAM_NODISCARD int count() const;
    YAM_NODISCARD void* at(int i) const;
    YAM_NODISCARD std::vector<void*> to_vector() const;
private:
    Yam::PtrArray* arr_{nullptr};
};

// ===========================================================================
// SECTION 21 — Event router (types match java-bridge.js events)
// ===========================================================================

class Event {
public:
    String type;
    JsonValue data;
    i64 timestamp{0};

    YAM_NODISCARD String get_str(const String& k, const String& def = "") const {
        auto* v = data.get(k);
        return v ? v->as_str(def) : def;
    }
    YAM_NODISCARD i64 get_i64(const String& k, i64 def = 0) const {
        auto* v = data.get(k);
        return v ? v->as_i64(def) : def;
    }
    YAM_NODISCARD u64 get_u64(const String& k, u64 def = 0) const {
        auto* v = data.get(k);
        return v ? static_cast<u64>(v->as_i64(static_cast<i64>(def))) : def;
    }
    YAM_NODISCARD bool get_bool(const String& k, bool def = false) const {
        auto* v = data.get(k);
        return v ? v->as_bool(def) : def;
    }
    YAM_NODISCARD f64 get_f64(const String& k, f64 def = 0) const {
        auto* v = data.get(k);
        return v ? v->as_f64(def) : def;
    }
    YAM_NODISCARD const JsonValue* get(const String& k) const { return data.get(k); }
    YAM_NODISCARD String dump() const { return data.stringify(); }
};

namespace events {
void on(const String& type, std::function<void(const Event&)> h);
void on_any(std::function<void(const Event&)> h);
void off(const String& type);
void clear();
std::vector<String> registered_types();
u64 count();
void dispatch(const Event& ev);
} // namespace events

// ===========================================================================
// SECTION 22 — Trace / Breakpoint / Param / Chunk / Watch / Replay / User
// ===========================================================================

namespace trace {
struct Target {
    u64 trace_id{0};
    String class_name;
    String method_name;
    String signature;
    bool   paused{false};
};
Result<u64> add(const String& cls, const String& method, const String& sig,
                i32 stack_depth = 0, i32 skip = 1, i32 min_ms = 0);
Result<u64> add_class(const String& cls, const String& pattern,
                      i32 stack_depth = 0, i32 skip = 1, i32 min_ms = 0);
Result<void> remove(u64 id);
Result<void> remove_range(u64 first, u64 last);
Result<void> clear();
Result<void> pause(u64 id);
Result<void> resume(u64 id);
std::vector<Target> active();
} // namespace trace

namespace bp {
struct Hit {
    u64    bp_id{0};
    u64    trace_id{0};
    String class_name;
    String method_name;
    i64    call_index{0};
    u64    thread_id{0};
    i64    timestamp{0};
    JsonValue args;
    JsonValue this_obj;
    JsonValue stack;
};
void set_handler(std::function<void(const Hit&)> h);
Result<void> set(u64 trace_id, bool enabled, i32 every = 1);
Result<void> continue_bp(u64 bp_id);
Result<void> throw_bp(u64 bp_id, const String& msg);
Result<void> continue_with_args(u64 bp_id, const std::vector<JsonValue>& args);
Result<void> modify_this(u64 bp_id, const std::vector<JsonValue>& fields);
void cancel_all();
void set_timeout(i64 ms);
} // namespace bp

namespace params {
Result<void> set(u64 trace_id, i32 index, const String& type, const String& value);
Result<void> clear(u64 trace_id, i32 index);
Result<void> set_spec(u64 trace_id, i32 index, const JsonValue& spec);
} // namespace params

namespace chunks {
struct Session {
    u64    id{0};
    String kind;
    usize  total{0};
    usize  received{0};
    String buffer;
    i64    started_ms{0};
};
void set_full_handler(std::function<void(const String& kind, const String& payload)> h);
std::vector<Session> active();
} // namespace chunks

namespace watch {
struct Change {
    u64    watch_id{0};
    u64    handle_id{0};
    String field_name;
    u64    change_count{0};
    JsonValue snapshot;
};
void on_change(std::function<void(const Change&)> cb);
Result<void> start(u64 handle_id, const String& field, i64 interval_ms = 250);
Result<void> stop(u64 watch_id);
Result<void> stop_all();
} // namespace watch

namespace user_scripts {
struct Loaded {
    String name;
    usize  size{0};
    bool   ok{false};
    String error;
    i64    duration_ms{0};
    i64    loaded_at{0};
    u64    call_count{0};
};
Result<void> run(const String& name, const String& code);
Result<void> run_batch(const std::vector<std::pair<String,String>>& scripts);
std::vector<Loaded> list();
void clear();
} // namespace user_scripts

namespace replay {
struct ResultItem {
    i64 index{0};
    bool ok{false};
    i64 duration_ms{0};
    String error;
};
struct ReplayResult {
    u64 replay_id{0};
    bool ok{false};
    String error;
    i64 total_ms{0};
    std::vector<ResultItem> results;
};
Result<void> run(u64 replay_id, const String& cls, const String& method,
                 const String& sig, bool is_static, u64 inst_handle,
                 const std::vector<JsonValue>& args, i32 times = 1,
                 i32 interleave_ms = 0, bool stop_on_error = false);
void on_result(std::function<void(const ReplayResult&)> cb);
} // namespace replay

// ===========================================================================
// SECTION 23 — Console
// ===========================================================================

namespace console {

struct EvalResult {
    bool   ok{false};
    String result;
    String error;
    i64    duration_ms{0};
};

class JsConsole {
public:
    static JsConsole& instance();
    Result<void> open();
    Result<void> close();
    YAM_NODISCARD bool is_open() const noexcept { return open_; }
    Result<EvalResult> eval(const String& code, i64 timeout_ms = 5000);
    std::vector<String> take_output();
    void set_sink(std::function<void(const String&, const String&)> cb);
private:
    JsConsole();
    ~JsConsole();
    bool open_{false};
    std::mutex mu_;
    std::vector<String> buffer_;
    std::function<void(const String&, const String&)> sink_;
    std::mutex sink_mu_;
};

class CppConsole {
public:
    using Command = std::function<String(const std::vector<String>& args)>;
    static CppConsole& instance();
    void register_command(const String& name, Command fn);
    void unregister_command(const String& name);
    void clear();
    Result<String> execute(const String& line);
    Result<String> call(const String& name, const std::vector<String>& args);
    std::vector<String> commands() const;
    void install_builtins();
private:
    CppConsole();
    ~CppConsole();
    mutable std::mutex mu_;
    std::unordered_map<String, Command> commands_;
};

class Console {
public:
    static Console& instance();
    Result<void> start();
    Result<void> stop();
    YAM_NODISCARD bool running() const noexcept { return running_; }
    JsConsole&  js();
    CppConsole& cpp();
private:
    Console() = default;
    ~Console() = default;
    bool running_{false};
};
inline Console& console() { return Console::instance(); }

} // namespace console

// ===========================================================================
// SECTION 24 — Entry / FinalGlue / YAM
// ===========================================================================

struct EntryOptions {
    LogLevel   log_level{LogLevel::Info};
    bool       enable_java{true};
    bool       enable_console{true};
    bool       enable_debugger{false};
    usize      worker_threads{0};
    String     script_name{"yam_entry"};
    BackendKind backend{BackendKind::QJS};
};

class Entry {
public:
    static Result<void> start(const EntryOptions& opt = {});
    static Result<void> stop();
    YAM_NODISCARD static bool started();
    YAM_NODISCARD static const EntryOptions& options();
};

class FinalGlue {
public:
    static Result<void> initialize_all(const EntryOptions& opt = {});
    static Result<void> shutdown_all();
    YAM_NODISCARD static bool initialized();
};

class YAM {
public:
    static Result<void> init(const EntryOptions& opt = {});
    static Result<void> shutdown();
    YAM_NODISCARD static bool started();
};

// ===========================================================================
// SECTION 25 — Diag
// ===========================================================================

struct DiagSnapshot {
    bool   runtime_init{false};
    bool   java_ready{false};
    usize  registry_entries{0};
    usize  registry_bytes{0};
    usize  java_hooks_count{0};
    usize  channels_count{0};
    i64    uptime_ms{0};
};

class RuntimeDiag {
public:
    static DiagSnapshot snapshot();
    static String to_json(const DiagSnapshot& s);
    static String to_string(const DiagSnapshot& s);
};

// ===========================================================================
// SECTION 26 — InstallEventRouter
// ===========================================================================

void install_event_router();

} // namespace yam

#endif // YAM_HPP
