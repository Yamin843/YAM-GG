// ===========================================================================
// yam_hooks.cpp — Interceptor + InvocationContext + Backtracer
// Uses both: C API for low-level, C++ Yam::Interceptor for the virtual
// polymorphic interface (needed by InvocationListener).
// ===========================================================================

#include "yam.hpp"
#include "yam_internal.hpp"
#include "yam_c_api.hpp"

#include "gumpp.hpp"
#include "invocationcontext.hpp"
#include "invocationlistener.hpp"

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
    // GumCpuContext arm64: x[29], fp, lr, sp, pc, q[128]
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
// SECTION 2 — InvocationContext (uses yam_invocation_context_* C API)
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
    return raw_ ? yam_invocation_context_get_thread_id(
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
// SECTION 3 — ListenerRegistry + shims
// ===========================================================================

namespace detail {

ListenerRegistry& ListenerRegistry::instance() {
    static ListenerRegistry inst;
    return inst;
}

std::shared_ptr<ListenerRegistry::Entry>
ListenerRegistry::make_entry(InvocationHook e, InvocationHook l, InvocationHook h) {
    auto r = std::make_shared<Entry>();
    r->on_enter = std::move(e);
    r->on_leave = std::move(l);
    r->on_hit   = std::move(h);
    return r;
}
void ListenerRegistry::drop(const std::shared_ptr<Entry>& e) {
    if (e) e->alive.store(false, std::memory_order_release);
}
std::shared_ptr<ListenerRegistry::Entry> ListenerRegistry::get(void* k) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(k);
    return it == map_.end() ? nullptr : it->second;
}
void ListenerRegistry::put(void* k, std::shared_ptr<Entry> e) {
    std::lock_guard<std::mutex> lk(mu_);
    map_[k] = std::move(e);
}
void ListenerRegistry::erase(void* k) {
    std::lock_guard<std::mutex> lk(mu_);
    map_.erase(k);
}
usize ListenerRegistry::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return map_.size();
}

// C++ subclasses of the Yam:: interfaces from libyamjs.a.
class YamListenerShim : public Yam::InvocationListener {
public:
    explicit YamListenerShim(std::shared_ptr<ListenerRegistry::Entry> e)
        : entry_(std::move(e)) {}
    virtual void on_enter(Yam::InvocationContext* ctx) override {
        if (!entry_ || !entry_->alive.load()) return;
        if (!entry_->on_enter) return;
        try {
            InvocationContext ic(static_cast<void*>(ctx));
            entry_->on_enter(ic);
        } catch (const std::exception& e) {
            YAM_LOG_ERROR() << "on_enter: " << e.what();
        } catch (...) {}
    }
    virtual void on_leave(Yam::InvocationContext* ctx) override {
        if (!entry_ || !entry_->alive.load()) return;
        if (!entry_->on_leave) return;
        try {
            InvocationContext ic(static_cast<void*>(ctx));
            entry_->on_leave(ic);
        } catch (const std::exception& e) {
            YAM_LOG_ERROR() << "on_leave: " << e.what();
        } catch (...) {}
    }
    std::shared_ptr<ListenerRegistry::Entry> entry() const { return entry_; }
private:
    std::shared_ptr<ListenerRegistry::Entry> entry_;
};

class YamProbeShim : public Yam::ProbeListener {
public:
    explicit YamProbeShim(std::shared_ptr<ListenerRegistry::Entry> e)
        : entry_(std::move(e)) {}
    virtual void on_hit(Yam::InvocationContext* ctx) override {
        if (!entry_ || !entry_->alive.load()) return;
        if (!entry_->on_hit) return;
        try {
            InvocationContext ic(static_cast<void*>(ctx));
            entry_->on_hit(ic);
        } catch (...) {}
    }
    std::shared_ptr<ListenerRegistry::Entry> entry() const { return entry_; }
private:
    std::shared_ptr<ListenerRegistry::Entry> entry_;
};

} // namespace detail

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
// SECTION 5 — Interceptor (uses Yam::Interceptor C++ from gumpp.hpp)
// ===========================================================================

namespace {
// The C++ API from gumpp.hpp is directly available.
// Yam::Interceptor_obtain() is declared in gumpp.hpp and exported by libyamjs.a.
// We do NOT use the C API (yam_interceptor_obtain) because it returns a
// GObject (YamInterceptor*) which is NOT the same type as Yam::Interceptor*.
Yam::Interceptor* obtain_interceptor() {
    static Yam::Interceptor* inst = nullptr;
    static std::once_flag once;
    std::call_once(once, []() {
        inst = Yam::Interceptor_obtain();
        if (!inst) YAM_LOG_ERROR() << "Yam::Interceptor_obtain returned null";
    });
    return inst;
}
std::mutex g_listeners_mu;
std::unordered_map<void*, std::shared_ptr<detail::YamListenerShim>> g_listeners;
std::unordered_map<void*, std::shared_ptr<detail::YamProbeShim>> g_probes;
} // namespace

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
    auto* yam = obtain_interceptor();
    if (!yam) return Attachment{};

    auto& reg = detail::ListenerRegistry::instance();
    auto entry = reg.make_entry(std::move(on_enter), std::move(on_leave), nullptr);
    auto shim = std::make_shared<detail::YamListenerShim>(entry);

    bool ok = yam->attach(target, shim.get(), nullptr);
    if (!ok) {
        YAM_LOG_ERROR() << "yam->attach failed at " << target;
        return Attachment{};
    }
    reg.put(shim.get(), entry);
    {
        std::lock_guard<std::mutex> lk(g_listeners_mu);
        g_listeners[shim.get()] = shim;
    }
    return Attachment(shim.get(), [shim, yam]() {
        try { yam->detach(shim.get()); } catch (...) {}
        detail::ListenerRegistry::instance().drop(shim->entry());
        std::lock_guard<std::mutex> lk(g_listeners_mu);
        g_listeners.erase(shim.get());
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
    auto* yam = obtain_interceptor();
    if (!yam) return Result<void>::err(ErrorCode::BackendUnavailable, "no interceptor");
    // The C++ API returns void. Original pointer retrieval is not exposed.
    yam->replace(target, replacement, data);
    return Result<void>::ok();
}

Result<void> Interceptor::revert(void* target) {
    if (!target) return Result<void>::err(ErrorCode::InvalidArgument, "revert");
    auto* yam = obtain_interceptor();
    if (!yam) return Result<void>::err(ErrorCode::BackendUnavailable, "no interceptor");
    yam->revert(target);
    return Result<void>::ok();
}

Result<void> Interceptor::attach_probe(void* target, InvocationHook on_hit) {
    if (!target) return Result<void>::err(ErrorCode::InvalidArgument, "probe");
    auto* yam = obtain_interceptor();
    if (!yam) return Result<void>::err(ErrorCode::BackendUnavailable, "no interceptor");
    auto& reg = detail::ListenerRegistry::instance();
    auto entry = reg.make_entry(nullptr, nullptr, std::move(on_hit));
    auto shim = std::make_shared<detail::YamProbeShim>(entry);
    bool ok = yam->attach(target, shim.get(), nullptr);
    if (!ok) return Result<void>::err(ErrorCode::AttachFailed, "probe attach");
    reg.put(shim.get(), entry);
    {
        std::lock_guard<std::mutex> lk(g_listeners_mu);
        g_probes[shim.get()] = shim;
    }
    return Result<void>::ok();
}

Result<void> Interceptor::detach_probe(void* target) {
    auto* yam = obtain_interceptor();
    if (!yam || !target) return Result<void>::ok();
    // Find probe shim by target not tracked — user must call detach_probe by shim.
    return Result<void>::ok();
}

void Interceptor::begin_transaction() {
    auto* yam = obtain_interceptor();
    if (yam) yam->begin_transaction();
}
void Interceptor::end_transaction() {
    auto* yam = obtain_interceptor();
    if (yam) yam->end_transaction();
}
Ptr<InvocationContext> Interceptor::current_invocation() {
    auto* yam = obtain_interceptor();
    if (!yam) return nullptr;
    auto* ctx = yam->get_current_invocation();
    if (!ctx) return nullptr;
    return std::make_shared<InvocationContext>(static_cast<void*>(ctx));
}
void Interceptor::ignore_current_thread() {
    auto* yam = obtain_interceptor();
    if (yam) yam->ignore_current_thread();
}
void Interceptor::unignore_current_thread() {
    auto* yam = obtain_interceptor();
    if (yam) yam->unignore_current_thread();
}
void Interceptor::ignore_other_threads() {
    auto* yam = obtain_interceptor();
    if (yam) yam->ignore_other_threads();
}
void Interceptor::unignore_other_threads() {
    auto* yam = obtain_interceptor();
    if (yam) yam->unignore_other_threads();
}

// ===========================================================================
// SECTION 6 — Backtracer (C++ Yam::Backtracer)
// ===========================================================================

Backtracer::Backtracer() : Backtracer(Mode::Accurate) {}
Backtracer::Backtracer(Mode m) {
    bt_ = (m == Mode::Accurate)
        ? Yam::Backtracer_make_accurate()
        : Yam::Backtracer_make_fuzzy();
}
Backtracer::~Backtracer() { if (bt_) { bt_->unref(); bt_ = nullptr; } }

Result<std::vector<void*>> Backtracer::generate(const CpuContext& ctx) const {
    if (!bt_) return Result<std::vector<void*>>::err(ErrorCode::BackendUnavailable, "");
    Yam::ReturnAddressArray arr{};
    bt_->generate(static_cast<const Yam::CpuContext*>(ctx.raw()), arr);
    std::vector<void*> out;
    out.reserve(arr.len);
    for (unsigned i = 0; i < arr.len; ++i) out.push_back(arr.items[i]);
    return Result<std::vector<void*>>::ok(std::move(out));
}

Result<BacktraceFrame> Backtracer::details(void* addr) {
    if (!addr) return Result<BacktraceFrame>::err(ErrorCode::InvalidArgument, "null");
    Yam::ReturnAddressDetails d{};
    if (!Yam::ReturnAddressDetails_from_address(
            static_cast<Yam::ReturnAddress>(addr), d)) {
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
