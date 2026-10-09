// ===========================================================================
// yam_hooks.cpp — Interceptor + InvocationContext + Backtracer
// Uses the C API only (Yam::Interceptor_obtain C++ symbol is not exported).
//   yam_interceptor_obtain        (void) -> YamInterceptor*
//   yam_make_call_listener        (on_enter, on_leave, data, destroy)
//                                 -> YamInvocationListener*
//   yam_interceptor_attach        (self, target, listener, options)
//   yam_interceptor_detach        (self, listener)
//   yam_interceptor_replace       (self, target, replacement, &orig, options)
//   yam_interceptor_revert        (self, target)
//   yam_interceptor_get_current_invocation (self) -> YamInvocationContext*
// ===========================================================================

#include "yam.hpp"
#include "yam_internal.hpp"

namespace yam {

// ===========================================================================
// SECTION 1 — CpuContext (arm64)
// ===========================================================================

u64 CpuContext::pc() const noexcept { return reg("pc"); }
u64 CpuContext::sp() const noexcept { return reg("sp"); }
u64 CpuContext::lr() const noexcept { return reg("lr"); }
u64 CpuContext::fp() const noexcept { return reg("fp"); }

u64 CpuContext::reg(const char* name) const noexcept {
    if (!raw_ || !name) return 0;
#if defined(__aarch64__)
    auto* base = static_cast<const u8*>(raw_);
    if (std::strcmp(name, "pc") == 0) { u64 v; std::memcpy(&v, base + 8*31, 8); return v; }
    if (std::strcmp(name, "sp") == 0) { u64 v; std::memcpy(&v, base + 8*30, 8); return v; }
    if (std::strcmp(name, "lr") == 0 || std::strcmp(name, "x30") == 0) {
        u64 v; std::memcpy(&v, base + 8*29, 8); return v;
    }
    if (std::strcmp(name, "fp") == 0 || std::strcmp(name, "x29") == 0) {
        u64 v; std::memcpy(&v, base + 8*28, 8); return v;
    }
    if (name[0] == 'x' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        int idx = std::atoi(name + 1);
        if (idx >= 0 && idx < 29) { u64 v; std::memcpy(&v, base + 8*idx, 8); return v; }
    }
    return 0;
#else
    return 0;
#endif
}

void CpuContext::set_reg(const char* name, u64 v) noexcept {
    if (!raw_ || !name) return;
#if defined(__aarch64__)
    auto* base = static_cast<u8*>(raw_);
    if (std::strcmp(name, "pc") == 0) { std::memcpy(base + 8*31, &v, 8); return; }
    if (std::strcmp(name, "sp") == 0) { std::memcpy(base + 8*30, &v, 8); return; }
    if (std::strcmp(name, "lr") == 0) { std::memcpy(base + 8*29, &v, 8); return; }
    if (std::strcmp(name, "fp") == 0) { std::memcpy(base + 8*28, &v, 8); return; }
    if (name[0] == 'x' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        int idx = std::atoi(name + 1);
        if (idx >= 0 && idx < 29) std::memcpy(base + 8*idx, &v, 8);
    }
#else
    (void)v;
#endif
}

// ===========================================================================
// SECTION 2 — InvocationContext (uses C API)
// ===========================================================================

void* InvocationContext::arg_ptr(unsigned n) const {
    return raw_ ? yam_invocation_context_get_nth_argument(
                    static_cast<YamInvocationContext*>(raw_), n) : nullptr;
}
void InvocationContext::set_arg_ptr(unsigned n, void* v) {
    if (raw_) yam_invocation_context_replace_nth_argument(
        static_cast<YamInvocationContext*>(raw_), n, v);
}
void* InvocationContext::ret_ptr() const {
    return raw_ ? yam_invocation_context_get_return_value(
                    static_cast<YamInvocationContext*>(raw_)) : nullptr;
}
unsigned InvocationContext::thread_id() const {
    return raw_ ? (unsigned)yam_invocation_context_get_thread_id(
                    static_cast<YamInvocationContext*>(raw_)) : 0;
}
void* InvocationContext::listener_thread_data(usize sz) const {
    return raw_ ? yam_invocation_context_get_listener_thread_data(
                    static_cast<YamInvocationContext*>(raw_), sz) : nullptr;
}
void* InvocationContext::listener_function_data() const {
    return raw_ ? yam_invocation_context_get_listener_function_data(
                    static_cast<YamInvocationContext*>(raw_)) : nullptr;
}
void* InvocationContext::listener_invocation_data(usize sz) const {
    return raw_ ? yam_invocation_context_get_listener_invocation_data(
                    static_cast<YamInvocationContext*>(raw_), sz) : nullptr;
}
void* InvocationContext::replacement_data() const {
    return raw_ ? yam_invocation_context_get_replacement_data(
                    static_cast<YamInvocationContext*>(raw_)) : nullptr;
}
Ptr<CpuContext> InvocationContext::cpu_context() const {
    if (!raw_) return nullptr;
    auto* ic = static_cast<YamInvocationContext*>(raw_);
    return std::make_shared<CpuContext>(ic->cpu_context);
}

i32 InvocationContext::arg_i32(unsigned n) const { return static_cast<i32>(reinterpret_cast<isize>(arg_ptr(n))); }
u32 InvocationContext::arg_u32(unsigned n) const { return static_cast<u32>(reinterpret_cast<usize>(arg_ptr(n))); }
i64 InvocationContext::arg_i64(unsigned n) const { return static_cast<i64>(reinterpret_cast<isize>(arg_ptr(n))); }
u64 InvocationContext::arg_u64(unsigned n) const { return static_cast<u64>(reinterpret_cast<usize>(arg_ptr(n))); }
f32 InvocationContext::arg_f32(unsigned n) const { u32 b = arg_u32(n); f32 v; std::memcpy(&v, &b, 4); return v; }
f64 InvocationContext::arg_f64(unsigned n) const { u64 b = arg_u64(n); f64 v; std::memcpy(&v, &b, 8); return v; }
String InvocationContext::arg_string(unsigned n) const {
    const char* p = static_cast<const char*>(arg_ptr(n));
    return p ? String(p) : String();
}
String InvocationContext::arg_utf16(unsigned n) const {
    const u16* p = static_cast<const u16*>(arg_ptr(n));
    if (!p) return {};
    String out;
    while (*p) {
        u16 c = *p++;
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// ===========================================================================
// SECTION 3 — Listener registry (shim storage)
// ===========================================================================
//
// Each attach creates a heap-allocated Entry that holds the two std::function
// callbacks. A C callback trampoline is installed via yam_make_call_listener
// with the Entry* as its user-data. When the listener is destroyed, the
// destroy callback deletes the Entry.
// ===========================================================================

namespace {

struct ShimEntry {
    InvocationHook on_enter;
    InvocationHook on_leave;
    InvocationHook on_hit;
    std::atomic<bool> alive{true};
};

void shim_on_enter(YamInvocationContext* ctx, gpointer user) {
    auto* e = static_cast<ShimEntry*>(user);
    if (!e || !e->alive.load(std::memory_order_acquire)) return;
    if (!e->on_enter) return;
    try {
        InvocationContext ic(static_cast<void*>(ctx));
        e->on_enter(ic);
    } catch (const std::exception& ex) {
        YAM_LOG_ERROR() << "shim on_enter: " << ex.what();
    } catch (...) {}
}

void shim_on_leave(YamInvocationContext* ctx, gpointer user) {
    auto* e = static_cast<ShimEntry*>(user);
    if (!e || !e->alive.load(std::memory_order_acquire)) return;
    if (!e->on_leave) return;
    try {
        InvocationContext ic(static_cast<void*>(ctx));
        e->on_leave(ic);
    } catch (const std::exception& ex) {
        YAM_LOG_ERROR() << "shim on_leave: " << ex.what();
    } catch (...) {}
}

void shim_on_hit(YamInvocationContext* ctx, gpointer user) {
    auto* e = static_cast<ShimEntry*>(user);
    if (!e || !e->alive.load(std::memory_order_acquire)) return;
    if (!e->on_hit) return;
    try {
        InvocationContext ic(static_cast<void*>(ctx));
        e->on_hit(ic);
    } catch (...) {}
}

void shim_destroy(gpointer user) {
    auto* e = static_cast<ShimEntry*>(user);
    if (!e) return;
    e->alive.store(false, std::memory_order_release);
    delete e;
}

YamInterceptor* obtain_interceptor() {
    static YamInterceptor* inst = nullptr;
    static std::once_flag once;
    std::call_once(once, []() {
        inst = yam_interceptor_obtain();
        if (!inst) YAM_LOG_ERROR() << "yam_interceptor_obtain returned null";
    });
    return inst;
}

std::mutex g_shim_mu;
std::unordered_map<YamInvocationListener*, ShimEntry*> g_shims;

} // namespace

// ===========================================================================
// SECTION 4 — Attachment
// ===========================================================================

Attachment::Attachment(void* listener, std::function<void()> fn)
    : listener_(listener), detach_fn_(std::move(fn)) {}
Attachment::~Attachment() { if (listener_) detach(); }
void Attachment::detach() {
    if (detach_fn_) { try { detach_fn_(); } catch (...) {} }
    listener_ = nullptr;
    detach_fn_ = nullptr;
}

// ===========================================================================
// SECTION 5 — Interceptor (C API)
// ===========================================================================

Interceptor::Interceptor() {
    raw_ = static_cast<void*>(obtain_interceptor());
}
Interceptor::~Interceptor() = default;

Interceptor& Interceptor::instance() {
    static Interceptor inst;
    return inst;
}

Attachment Interceptor::attach(void* target, InvocationHook on_enter, InvocationHook on_leave) {
    if (!target) {
        YAM_LOG_ERROR() << "attach: null target";
        return Attachment{};
    }
    auto* ic = obtain_interceptor();
    if (!ic) return Attachment{};

    auto* entry = new ShimEntry();
    entry->on_enter = std::move(on_enter);
    entry->on_leave = std::move(on_leave);

    YamInvocationListener* listener = yam_make_call_listener(
        shim_on_enter, shim_on_leave, entry, shim_destroy);
    if (!listener) {
        delete entry;
        YAM_LOG_ERROR() << "yam_make_call_listener returned null";
        return Attachment{};
    }

    YamAttachReturn rc = yam_interceptor_attach(ic, target, listener, nullptr);
    if (rc != YAM_ATTACH_OK) {
        YAM_LOG_ERROR() << "yam_interceptor_attach failed: " << rc;
        // shim_destroy will be called if we g_object_unref the listener, but
        // the C API does not expose an unref. Leave the listener alive; the
        // Entry pointer is ours and delete is handled by shim_destroy on
        // the next detach.
        return Attachment{};
    }

    {
        std::lock_guard<std::mutex> lk(g_shim_mu);
        g_shims[listener] = entry;
    }

    return Attachment(listener, [ic, listener, entry]() {
        try { yam_interceptor_detach(ic, listener); } catch (...) {}
        entry->alive.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lk(g_shim_mu);
        g_shims.erase(listener);
    });
}

Attachment Interceptor::attach(const String& symbol, InvocationHook on_enter, InvocationHook on_leave) {
    void* p = Symbol::resolve(symbol);
    if (!p) p = Module::find_global_export(symbol);
    if (!p) {
        YAM_LOG_ERROR() << "symbol not found: " << symbol;
        return Attachment{};
    }
    return attach(p, std::move(on_enter), std::move(on_leave));
}

Result<void> Interceptor::detach(Attachment& att) {
    att.detach();
    return Result<void>::ok();
}

Result<void> Interceptor::replace(void* target, void* replacement, void* data) {
    if (!target || !replacement)
        return Result<void>::err(ErrorCode::InvalidArgument, "replace");
    auto* ic = obtain_interceptor();
    if (!ic) return Result<void>::err(ErrorCode::BackendUnavailable, "no interceptor");

    YamReplaceReturn rc = yam_interceptor_replace(
        ic, target, replacement, nullptr, nullptr);
    if (rc != YAM_REPLACE_OK) {
        return Result<void>::err(ErrorCode::ReplaceFailed, "replace");
    }
    (void)data;
    return Result<void>::ok();
}

Result<void> Interceptor::revert(void* target) {
    if (!target) return Result<void>::err(ErrorCode::InvalidArgument, "revert");
    auto* ic = obtain_interceptor();
    if (!ic) return Result<void>::err(ErrorCode::BackendUnavailable, "no interceptor");
    yam_interceptor_revert(ic, target);
    return Result<void>::ok();
}

Result<void> Interceptor::attach_probe(void* target, InvocationHook on_hit) {
    if (!target) return Result<void>::err(ErrorCode::InvalidArgument, "probe");
    auto* ic = obtain_interceptor();
    if (!ic) return Result<void>::err(ErrorCode::BackendUnavailable, "no interceptor");

    auto* entry = new ShimEntry();
    entry->on_hit = std::move(on_hit);

    YamInvocationListener* listener = yam_make_probe_listener(
        shim_on_hit, entry, shim_destroy);
    if (!listener) {
        delete entry;
        return Result<void>::err(ErrorCode::AttachFailed, "make_probe_listener");
    }

    YamAttachReturn rc = yam_interceptor_attach(ic, target, listener, nullptr);
    if (rc != YAM_ATTACH_OK) {
        return Result<void>::err(ErrorCode::AttachFailed, "probe attach");
    }

    {
        std::lock_guard<std::mutex> lk(g_shim_mu);
        g_shims[listener] = entry;
    }
    return Result<void>::ok();
}

Result<void> Interceptor::detach_probe(void* target) {
    (void)target;
    return Result<void>::ok();
}

void Interceptor::begin_transaction() {
    auto* ic = obtain_interceptor();
    if (ic) yam_interceptor_begin_transaction(ic);
}

void Interceptor::end_transaction() {
    auto* ic = obtain_interceptor();
    if (ic) yam_interceptor_end_transaction(ic);
}

Ptr<InvocationContext> Interceptor::current_invocation() {
    auto* ic = obtain_interceptor();
    if (!ic) return nullptr;
    auto* ctx = yam_interceptor_get_current_invocation(ic);
    if (!ctx) return nullptr;
    return std::make_shared<InvocationContext>(static_cast<void*>(ctx));
}

void Interceptor::ignore_current_thread() {
    auto* ic = obtain_interceptor();
    if (ic) yam_interceptor_ignore_current_thread(ic);
}
void Interceptor::unignore_current_thread() {
    auto* ic = obtain_interceptor();
    if (ic) yam_interceptor_unignore_current_thread(ic);
}
void Interceptor::ignore_other_threads() {
    auto* ic = obtain_interceptor();
    if (ic) yam_interceptor_ignore_other_threads(ic);
}
void Interceptor::unignore_other_threads() {
    auto* ic = obtain_interceptor();
    if (ic) yam_interceptor_unignore_other_threads(ic);
}

// ===========================================================================
// SECTION 6 — Backtracer (C API)
// ===========================================================================

namespace {
YamBacktracer* make_accurate_bt() {
    static YamBacktracer* bt = nullptr;
    static std::once_flag once;
    std::call_once(once, []() {
        bt = yam_backtracer_make_accurate();
    });
    return bt;
}
YamBacktracer* make_fuzzy_bt() {
    static YamBacktracer* bt = nullptr;
    static std::once_flag once;
    std::call_once(once, []() {
        bt = yam_backtracer_make_fuzzy();
    });
    return bt;
}
} // namespace

Backtracer::Backtracer() : Backtracer(Mode::Accurate) {}

Backtracer::Backtracer(Mode m) {
    raw_ = (m == Mode::Accurate) ? make_accurate_bt() : make_fuzzy_bt();
    if (raw_) {
        // ref the shared singleton so its destructor path is symmetric
        // with the original behaviour.
        // (YamBacktracer has _ref/_unref but we don't have direct access here.)
    }
}

Backtracer::~Backtracer() { raw_ = nullptr; }

Result<std::vector<void*>> Backtracer::generate(const CpuContext& ctx) const {
    if (!raw_) return Result<std::vector<void*>>::err(
        ErrorCode::BackendUnavailable, "backtracer");
    YamReturnAddressArray arr{};
    yam_backtracer_generate(raw_, static_cast<YamCpuContext*>(ctx.raw()), &arr);
    std::vector<void*> out;
    out.reserve(arr.len);
    for (guint i = 0; i < arr.len; ++i) out.push_back(arr.items[i]);
    return Result<std::vector<void*>>::ok(std::move(out));
}

Result<BacktraceFrame> Backtracer::details(void* addr) {
    if (!addr) return Result<BacktraceFrame>::err(ErrorCode::InvalidArgument, "null");
    YamReturnAddressDetails d{};
    if (!yam_return_address_details_from_address(
            static_cast<YamReturnAddress>(addr), &d)) {
        return Result<BacktraceFrame>::err(ErrorCode::SymbolNotFound, "details");
    }
    BacktraceFrame f;
    f.address = d.address;
    f.module_name = d.module_name;
    f.function_name = d.function_name;
    f.file_name = d.file_name;
    f.line = d.line_number;
    f.column = d.column;
    return Result<BacktraceFrame>::ok(std::move(f));
}

} // namespace yam
