// ===========================================================================
// yam_subsystems.cpp
// ===========================================================================

#include "yam_subsystems.hpp"
#include "yam_internal.hpp"

#include <cstring>

namespace yam {

// ===========================================================================
// CModule
// ===========================================================================
CModule::CModule(const String& source, const String& name,
                 void* near, usize max_distance) {
    YamCModuleOptions opts{};
    if (near && max_distance) {
        opts.range.base_address = reinterpret_cast<YamAddress>(near);
        opts.range.size = max_distance;
    }
    raw_ = yam_cmodule_new(source.c_str(), &opts,
                            name.empty() ? nullptr : name.c_str());
}
CModule::~CModule() {
    if (raw_) { yam_cmodule_free(raw_); raw_ = nullptr; }
}
Result<void> CModule::link() {
    if (!raw_) return Result<void>::err(ErrorCode::InvalidArgument, "no cmodule");
    if (!yam_cmodule_link(raw_))
        return Result<void>::err(ErrorCode::InternalError, "cmodule link failed");
    return Result<void>::ok();
}
void* CModule::symbol(const String& name) const {
    if (!raw_ || name.empty()) return nullptr;
    return yam_cmodule_find_symbol_by_name(raw_, name.c_str());
}
YamMemoryRange CModule::range() const {
    YamMemoryRange out{};
    if (!raw_) return out;
    YamMemoryRange* r = yam_cmodule_get_range(raw_);
    if (r) out = *r;
    return out;
}
void CModule::add_symbol(const String& name, void* value) {
    if (raw_) yam_cmodule_add_symbol(raw_, name.c_str(), value);
}
namespace {
gboolean cm_symbol_visitor(const YamCModuleSymbolDetails* d, gpointer user) {
    auto* v = static_cast<std::vector<std::pair<String, void*>>*>(user);
    v->emplace_back(d->name ? d->name : "", d->value);
    return 1;
}
}
std::vector<std::pair<String, void*>> CModule::symbols() const {
    std::vector<std::pair<String, void*>> out;
    if (!raw_) return out;
    yam_cmodule_enumerate_symbols(raw_, cm_symbol_visitor, &out);
    return out;
}
void CModule::drop_metadata() {
    if (raw_) yam_cmodule_drop_metadata(raw_);
}

// ===========================================================================
// Cloak
// ===========================================================================
bool Cloak::has_range_containing(void* address) {
    return yam_cloak_has_range_containing(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(address))) != 0;
}
void Cloak::add_range(void* base, usize size) {
    YamMemoryRange r{};
    r.base_address = static_cast<YamAddress>(reinterpret_cast<uintptr_t>(base));
    r.size = size;
    yam_cloak_add_range(&r);
}
void Cloak::remove_range(void* base, usize size) {
    YamMemoryRange r{};
    r.base_address = static_cast<YamAddress>(reinterpret_cast<uintptr_t>(base));
    r.size = size;
    yam_cloak_remove_range(&r);
}
void Cloak::clip_range(void*& base, usize& size) {
    YamMemoryRange r{};
    r.base_address = static_cast<YamAddress>(reinterpret_cast<uintptr_t>(base));
    r.size = size;
    GArray* g = yam_cloak_clip_range(&r);
    if (g) {
        if (g->len > 0) {
            auto* first = &g_array_index(g, YamMemoryRange, 0);
            base = reinterpret_cast<void*>(static_cast<uintptr_t>(first->base_address));
            size = first->size;
        } else {
            base = nullptr;
            size = 0;
        }
        g_array_unref(g);
    }
}
void Cloak::add_thread(u32 tid)    { yam_cloak_add_thread(static_cast<YamThreadId>(tid)); }
void Cloak::remove_thread(u32 tid) { yam_cloak_remove_thread(static_cast<YamThreadId>(tid)); }
bool Cloak::has_thread(u32 tid)    { return yam_cloak_has_thread(static_cast<YamThreadId>(tid)) != 0; }
void Cloak::add_fd(i32 fd)         { yam_cloak_add_file_descriptor(fd); }
void Cloak::remove_fd(i32 fd)      { yam_cloak_remove_file_descriptor(fd); }
bool Cloak::has_fd(i32 fd)         { return yam_cloak_has_file_descriptor(fd) != 0; }
bool Cloak::is_locked()            { return yam_cloak_is_locked() != 0; }

namespace {
std::vector<Cloak::MemoryRange>* g_cloak_ranges = nullptr;
std::vector<u32>* g_cloak_threads = nullptr;
std::vector<i32>* g_cloak_fds = nullptr;
}

static gboolean cloak_range_visitor(const YamMemoryRange* r, gpointer) {
    if (g_cloak_ranges && r) {
        Cloak::MemoryRange m;
        m.base = reinterpret_cast<void*>(static_cast<uintptr_t>(r->base_address));
        m.size = r->size;
        g_cloak_ranges->push_back(m);
    }
    return 1;
}
static gboolean cloak_thread_visitor(YamThreadId t, gpointer) {
    if (g_cloak_threads) g_cloak_threads->push_back(static_cast<u32>(t));
    return 1;
}
static gboolean cloak_fd_visitor(gint fd, gpointer) {
    if (g_cloak_fds) g_cloak_fds->push_back(static_cast<i32>(fd));
    return 1;
}

std::vector<Cloak::MemoryRange> Cloak::enumerate_ranges() {
    std::vector<MemoryRange> out;
    g_cloak_ranges = &out;
    yam_cloak_enumerate_ranges(cloak_range_visitor, nullptr);
    g_cloak_ranges = nullptr;
    return out;
}
std::vector<u32> Cloak::enumerate_threads() {
    std::vector<u32> out;
    g_cloak_threads = &out;
    yam_cloak_enumerate_threads(cloak_thread_visitor, nullptr);
    g_cloak_threads = nullptr;
    return out;
}
std::vector<i32> Cloak::enumerate_fds() {
    std::vector<i32> out;
    g_cloak_fds = &out;
    yam_cloak_enumerate_file_descriptors(cloak_fd_visitor, nullptr);
    g_cloak_fds = nullptr;
    return out;
}

// ===========================================================================
// BoundsChecker
// ===========================================================================
BoundsChecker::BoundsChecker() {
    raw_ = yam_bounds_checker_new(nullptr, nullptr);
}
BoundsChecker::~BoundsChecker() {
    if (raw_) { yam_bounds_checker_destroy(raw_); raw_ = nullptr; }
}
Result<void> BoundsChecker::attach(const String& apis) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "bc");
    yam_bounds_checker_attach(raw_, apis.empty() ? nullptr : apis.c_str());
    return Result<void>::ok();
}
Result<void> BoundsChecker::detach() {
    if (raw_) yam_bounds_checker_detach(raw_);
    return Result<void>::ok();
}
void BoundsChecker::set_front_alignment(u32 g) { if (raw_) yam_bounds_checker_set_front_alignment(raw_, g); }
void BoundsChecker::set_pool_size(u32 s)       { if (raw_) yam_bounds_checker_set_pool_size(raw_, s); }
u32 BoundsChecker::front_alignment() const     { return raw_ ? yam_bounds_checker_get_front_alignment(raw_) : 0; }
u32 BoundsChecker::pool_size() const           { return raw_ ? yam_bounds_checker_get_pool_size(raw_) : 0; }

// ===========================================================================
// AllocatorProbe
// ===========================================================================
AllocatorProbe::AllocatorProbe() { raw_ = yam_allocator_probe_new(); }
AllocatorProbe::~AllocatorProbe() { raw_ = nullptr; }
Result<void> AllocatorProbe::attach(const String& apis) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "ap");
    if (apis.empty()) yam_allocator_probe_attach(raw_);
    else yam_allocator_probe_attach_to_apis(raw_, apis.c_str());
    return Result<void>::ok();
}
Result<void> AllocatorProbe::detach() { if (raw_) yam_allocator_probe_detach(raw_); return Result<void>::ok(); }
void AllocatorProbe::suppress() { if (raw_) yam_allocator_probe_suppress(raw_); }

// ===========================================================================
// ApiResolver
// ===========================================================================
ApiResolver::ApiResolver(Type t) {
    const char* s = "module";
    switch (t) {
    case Type::Swift: s = "swift"; break;
    case Type::Objc:  s = "objc";  break;
    case Type::Default: s = "default"; break;
    default: s = "module"; break;
    }
    raw_ = yam_api_resolver_make(s);
}
ApiResolver::~ApiResolver() { raw_ = nullptr; }

namespace {
gboolean api_visitor(const YamApiDetails* d, gpointer user) {
    auto* v = static_cast<std::vector<ApiDetails>*>(user);
    ApiDetails a;
    a.address = reinterpret_cast<void*>(static_cast<uintptr_t>(d->address));
    a.name = d->name ? d->name : "";
    v->push_back(std::move(a));
    return 1;
}
}

Result<std::vector<ApiDetails>> ApiResolver::enumerate_matches(const String& query) {
    std::vector<ApiDetails> out;
    if (!raw_) return Result<std::vector<ApiDetails>>::err(
        ErrorCode::BackendUnavailable, "ar");
    yam_api_resolver_enumerate_matches(raw_, query.c_str(),
                                        api_visitor, &out, nullptr);
    return Result<std::vector<ApiDetails>>::ok(std::move(out));
}

// ===========================================================================
// SourceMap
// ===========================================================================
SourceMap::SourceMap(const String& json) { raw_ = yam_source_map_new(json.c_str()); }
SourceMap::~SourceMap() { if (raw_) { yam_source_map_free(raw_); raw_ = nullptr; } }
Result<SourceMap::Position> SourceMap::resolve(u32 line, u32 col) {
    if (!raw_) return Result<Position>::err(ErrorCode::BackendUnavailable, "sm");
    guint ol = 0, oc = 0;
    const gchar* name = nullptr;
    const gchar* src = nullptr;
    if (!yam_source_map_resolve(raw_, line, col, &ol, &oc, &name, &src))
        return Result<Position>::err(ErrorCode::SymbolNotFound, "resolve");
    Position p;
    p.line = ol; p.column = oc;
    p.name = name ? name : "";
    p.source = src ? src : "";
    return Result<Position>::ok(std::move(p));
}

// ===========================================================================
// TlsKey
// ===========================================================================
TlsKey::TlsKey() { key_ = yam_tls_key_new(); }
TlsKey::~TlsKey() { if (key_) { yam_tls_key_free(key_); key_ = 0; } }
void* TlsKey::get() const { return key_ ? yam_tls_key_get_value(key_) : nullptr; }
void  TlsKey::set(void* v) { if (key_) yam_tls_key_set_value(key_, v); }

// ===========================================================================
// EventSink
// ===========================================================================
namespace {
void event_sink_tramp(const YamEvent* e, YamCpuContext* /*ctx*/, gpointer user) {
    auto* cb = static_cast<EventSink::Callback*>(user);
    if (!cb || !*cb || !e) return;
    SinkEvent ev;
    ev.type = static_cast<u32>(e->type);
    (*cb)(ev);
}
}
EventSink::EventSink() { raw_ = yam_event_sink_make_default(); }
EventSink::EventSink(Callback cb) : cb_(std::move(cb)) {
    raw_ = yam_event_sink_make_from_callback(
        static_cast<YamEventType>(0xFFFFFFFFu),
        event_sink_tramp, &cb_, nullptr);
}
EventSink::~EventSink() { raw_ = nullptr; }
void EventSink::start() { if (raw_) yam_event_sink_start(raw_); }
void EventSink::stop()  { if (raw_) yam_event_sink_stop(raw_); }
void EventSink::flush() { if (raw_) yam_event_sink_flush(raw_); }
u32  EventSink::query_mask() const {
    if (!raw_) return 0;
    return static_cast<u32>(yam_event_sink_query_mask(raw_));
}

// ===========================================================================
// Exceptor
// ===========================================================================
Exceptor::Exceptor() { raw_ = yam_exceptor_obtain(); }
Exceptor::~Exceptor() = default;

namespace {
const char* exceptor_type_name(YamExceptionType t) {
    switch (t) {
    case YAM_EXCEPTION_ABORT:               return "abort";
    case YAM_EXCEPTION_ACCESS_VIOLATION:    return "access_violation";
    case YAM_EXCEPTION_GUARD_PAGE:          return "guard_page";
    case YAM_EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal_instruction";
    case YAM_EXCEPTION_STACK_OVERFLOW:      return "stack_overflow";
    case YAM_EXCEPTION_ARITHMETIC:          return "arithmetic";
    case YAM_EXCEPTION_BREAKPOINT:          return "breakpoint";
    case YAM_EXCEPTION_SINGLE_STEP:         return "single_step";
    case YAM_EXCEPTION_SYSTEM:              return "system";
    default:                                return "unknown";
    }
}
gboolean exceptor_trampoline(YamExceptionDetails* d, gpointer user) {
    auto* self = static_cast<Exceptor*>(user);
    if (!self || !d) return 0;
    ExceptionDetails ed;
    ed.thread_id = static_cast<u64>(d->thread_id);
    ed.type = static_cast<u32>(d->type);
    ed.address = d->address;
    ed.type_name = exceptor_type_name(d->type);
    self->dispatch(ed);
    return 1;
}
}

void Exceptor::dispatch(const ExceptionDetails& ed) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& p : handlers_) {
        try { p.second(ed); }
        catch (const std::exception& e) { YAM_LOG_ERROR() << "exceptor: " << e.what(); }
    }
}
Result<void> Exceptor::add(Handler h) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "exceptor");
    if (!h) return Result<void>::err(ErrorCode::InvalidArgument, "null handler");
    static std::atomic<u64> next_id{1};
    u64 id = next_id.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(mu_);
        handlers_[reinterpret_cast<void*>(id)] = std::move(h);
    }
    if (handlers_.size() == 1)
        yam_exceptor_add(raw_, exceptor_trampoline, this);
    return Result<void>::ok();
}
Result<void> Exceptor::remove() {
    if (!raw_) return Result<void>::ok();
    yam_exceptor_remove(raw_, exceptor_trampoline, this);
    { std::lock_guard<std::mutex> lk(mu_); handlers_.clear(); }
    return Result<void>::ok();
}
Result<void> Exceptor::set_mode(i32 mode) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "exceptor");
    yam_exceptor_set_mode(static_cast<YamExceptorMode>(mode));
    return Result<void>::ok();
}
Result<void> Exceptor::reset() {
    if (raw_) yam_exceptor_reset(raw_);
    return Result<void>::ok();
}
bool Exceptor::has_scope() const {
    if (!raw_) return false;
    YamThreadId tid = static_cast<YamThreadId>(yam_process_get_current_thread_id());
    return yam_exceptor_has_scope(raw_, tid) != 0;
}

// ===========================================================================
// Kernel — YamAddress on both directions
// ===========================================================================
bool Kernel::available() { return yam_kernel_api_is_available() != 0; }

void* Kernel::alloc_pages(u32 n) {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(yam_kernel_alloc_n_pages(n)));
}
void Kernel::free_pages(void* p) {
    yam_kernel_free_pages(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(p)));
}
usize Kernel::page_size() {
    return static_cast<usize>(yam_kernel_query_page_size());
}
bool Kernel::read(void* addr, void* out, usize sz) {
    if (!addr || !out || !sz) return false;
    gsize got = 0;
    guint8* p = yam_kernel_read(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(addr)), sz, &got);
    if (!p || got != sz) {
        if (p) yam_free(p);
        return false;
    }
    std::memcpy(out, p, sz);
    yam_free(p);
    return true;
}
bool Kernel::write(void* addr, const void* in, usize sz) {
    if (!addr || !in || !sz) return false;
    return yam_kernel_write(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(addr)),
        static_cast<const guint8*>(in), sz) != 0;
}
bool Kernel::mprotect(void* addr, usize sz, Protection p) {
    guint yp = YAM_PAGE_NO_ACCESS;
    if (has_flag(p, Protection::Read))  yp |= YAM_PAGE_READ;
    if (has_flag(p, Protection::Write)) yp |= YAM_PAGE_WRITE;
    if (has_flag(p, Protection::Exec))  yp |= YAM_PAGE_EXECUTE;
    return yam_kernel_try_mprotect(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(addr)),
        sz, static_cast<YamPageProtection>(yp)) != 0;
}
void* Kernel::base_address() {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(yam_kernel_find_base_address()));
}
void Kernel::set_base_address(void* a) {
    yam_kernel_set_base_address(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(a)));
}

namespace {
std::vector<String>* g_kmods = nullptr;
gboolean kmodule_v(const YamKernelModuleDetails* d, gpointer) {
    if (g_kmods && d && d->name) {
        g_kmods->emplace_back(d->name);
    }
    return 1;
}
}
std::vector<String> Kernel::enumerate_modules() {
    std::vector<String> out;
    if (!yam_kernel_api_is_available()) return out;
    g_kmods = &out;
    yam_kernel_enumerate_modules(kmodule_v, nullptr);
    g_kmods = nullptr;
    return out;
}

} // namespace yam

// ===========================================================================
// APPENDIX — الأقسام المستعادة
// ===========================================================================

namespace yam {

// ---------------------------------------------------------------------------
// ThreadRegistry — YamThreadDetails له حقول ظاهرة
// ---------------------------------------------------------------------------
static gboolean thread_enum_visitor(const YamThreadDetails* d, gpointer user) {
    auto* out = static_cast<std::vector<ThreadInfo>*>(user);
    if (!d || !out) return 0;
    ThreadInfo t;
    t.id = static_cast<u32>(d->id);
    t.name = d->name ? d->name : "";
    t.flags = static_cast<u32>(d->flags);
    t.state = static_cast<u32>(d->state);
    out->push_back(std::move(t));
    return 1;
}

std::vector<ThreadInfo> ThreadRegistry::enumerate() {
    std::vector<ThreadInfo> out;
    yam_process_enumerate_threads(thread_enum_visitor, &out,
                                   static_cast<YamThreadFlags>(0));
    return out;
}

Result<ThreadInfo> ThreadRegistry::find(u32 tid) {
    YamThreadDetails* d = yam_process_find_thread_by_id(
        static_cast<YamThreadId>(tid), static_cast<YamThreadFlags>(0));
    if (!d) return Result<ThreadInfo>::err(ErrorCode::SymbolNotFound, "tid");
    ThreadInfo t;
    t.id = static_cast<u32>(d->id);
    t.name = d->name ? d->name : "";
    t.flags = static_cast<u32>(d->flags);
    t.state = static_cast<u32>(d->state);
    yam_thread_details_free(d);
    return Result<ThreadInfo>::ok(std::move(t));
}

Result<std::vector<std::pair<void*, usize>>> ThreadRegistry::ranges(u32 /*tid*/) {
    YamMemoryRange buf[64] = {};
    guint n = yam_thread_try_get_ranges(buf, 64);
    std::vector<std::pair<void*, usize>> out;
    for (guint i = 0; i < n; ++i) {
        out.emplace_back(reinterpret_cast<void*>(buf[i].base_address),
                         buf[i].size);
    }
    return Result<std::vector<std::pair<void*, usize>>>::ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Linux — مبنية على yam_process_* + yam_thread_*
// ---------------------------------------------------------------------------
std::vector<Linux::PthreadSpec> Linux::enumerate_pthreads() {
    std::vector<PthreadSpec> out;
    for (auto& t : ThreadRegistry::enumerate()) {
        PthreadSpec s;
        s.tid = t.id;
        s.name = t.name;
        s.state = t.state;
        out.push_back(std::move(s));
    }
    return out;
}

Result<Linux::PthreadSpec> Linux::find(u32 tid) {
    auto r = ThreadRegistry::find(tid);
    if (!r) return Result<PthreadSpec>::err(r.error_code(), r.error_message());
    PthreadSpec s;
    s.tid = r.value().id;
    s.name = r.value().name;
    s.state = r.value().state;
    return Result<PthreadSpec>::ok(std::move(s));
}

bool Linux::suspend_thread(u32 tid) {
    return yam_thread_suspend(static_cast<YamThreadId>(tid), nullptr) != 0;
}
bool Linux::resume_thread(u32 tid) {
    return yam_thread_resume(static_cast<YamThreadId>(tid), nullptr) != 0;
}
bool Linux::set_hardware_breakpoint(u32 tid, u32 bp_id, void* addr) {
    (void)tid; (void)bp_id; (void)addr;
    return false;
}
bool Linux::unset_hardware_breakpoint(u32 tid, u32 bp_id) {
    (void)tid; (void)bp_id;
    return false;
}
bool Linux::set_hardware_watchpoint(u32 tid, u32 wp_id, void* addr,
                                     u32 size, u32 cond) {
    (void)tid; (void)wp_id; (void)addr; (void)size; (void)cond;
    return false;
}
bool Linux::unset_hardware_watchpoint(u32 tid, u32 wp_id) {
    (void)tid; (void)wp_id;
    return false;
}

// ---------------------------------------------------------------------------
// AllocationTracker
// ---------------------------------------------------------------------------
AllocationTracker::AllocationTracker() { raw_ = yam_allocation_tracker_new(); }
AllocationTracker::~AllocationTracker() { raw_ = nullptr; }
void AllocationTracker::begin(u32 flags) { if (raw_) yam_allocation_tracker_begin(raw_, flags); }
bool AllocationTracker::end() { return raw_ && yam_allocation_tracker_end(raw_); }
u32 AllocationTracker::block_count() const { return raw_ ? yam_allocation_tracker_peek_block_count(raw_) : 0; }
usize AllocationTracker::block_total_size() const { return raw_ ? yam_allocation_tracker_peek_block_total_size(raw_) : 0; }
std::vector<AllocationBlock> AllocationTracker::blocks() const {
    // YamAllocationBlock is declared but not defined in YAMJS.h — it is an
    // opaque type. The peek_block_list() API exposes only a raw pointer to
    // an unknown-layout array, so field-level iteration is not possible
    // through this header. Callers can still use block_count() and
    // block_total_size().
    return {};
}

// ---------------------------------------------------------------------------
// InstanceTracker
// ---------------------------------------------------------------------------
InstanceTracker::InstanceTracker() { raw_ = yam_instance_tracker_new(); }
InstanceTracker::~InstanceTracker() { raw_ = nullptr; }
void InstanceTracker::begin(u32 flags) { if (raw_) yam_instance_tracker_begin(raw_, flags); }
bool InstanceTracker::end() { return raw_ && yam_instance_tracker_end(raw_); }
u32 InstanceTracker::total_count() const { return raw_ ? yam_instance_tracker_peek_total_count(raw_) : 0; }
std::vector<std::pair<void*, void*>> InstanceTracker::instances() const {
    return {};
}

// ---------------------------------------------------------------------------
// Arm64Writer
// ---------------------------------------------------------------------------
Arm64Writer::Arm64Writer(void* code) { w_ = yam_arm64_writer_new(code); }
Arm64Writer::~Arm64Writer() {
    if (w_) { yam_arm64_writer_unref(w_); w_ = nullptr; }
}
void* Arm64Writer::cursor() const { return w_ ? yam_arm64_writer_cur(w_) : nullptr; }
u32   Arm64Writer::offset() const { return w_ ? yam_arm64_writer_offset(w_) : 0; }
void  Arm64Writer::reset(void* pc) { if (w_) yam_arm64_writer_reset(w_, pc); }
void  Arm64Writer::flush() { if (w_) yam_arm64_writer_flush(w_); }
void  Arm64Writer::skip(u32 n) { if (w_) yam_arm64_writer_skip(w_, n); }
void Arm64Writer::put_instruction(u32 i) { if (w_) yam_arm64_writer_put_instruction(w_, i); }
void Arm64Writer::put_bytes(const ByteVector& d) {
    if (w_ && !d.empty()) yam_arm64_writer_put_bytes(w_, d.data(), (guint)d.size());
}
void Arm64Writer::put_nop() { if (w_) yam_arm64_writer_put_nop(w_); }
void Arm64Writer::put_ret() { if (w_) yam_arm64_writer_put_ret(w_); }
void Arm64Writer::put_ret_reg(int r) { if (w_) yam_arm64_writer_put_ret_reg(w_, (arm64_reg)r); }
void Arm64Writer::put_mov_reg_reg(int d, int s) { if (w_) yam_arm64_writer_put_mov_reg_reg(w_, (arm64_reg)d, (arm64_reg)s); }
void Arm64Writer::put_mov_reg_u64(int d, u64 v) { if (w_) yam_arm64_writer_put_ldr_reg_u64(w_, (arm64_reg)d, v); }
void Arm64Writer::put_mov_reg_address(int d, void* a) { if (w_) yam_arm64_writer_put_ldr_reg_address(w_, (arm64_reg)d, reinterpret_cast<YamAddress>(a)); }
void Arm64Writer::put_mov_reg_nzcv(int r) { if (w_) yam_arm64_writer_put_mov_reg_nzcv(w_, (arm64_reg)r); }
void Arm64Writer::put_mov_nzcv_reg(int r) { if (w_) yam_arm64_writer_put_mov_nzcv_reg(w_, (arm64_reg)r); }
void Arm64Writer::put_add_reg_reg_imm(int d, int l, u64 r) { if (w_) yam_arm64_writer_put_add_reg_reg_imm(w_, (arm64_reg)d, (arm64_reg)l, r); }
void Arm64Writer::put_add_reg_reg_reg(int d, int l, int r) { if (w_) yam_arm64_writer_put_add_reg_reg_reg(w_, (arm64_reg)d, (arm64_reg)l, (arm64_reg)r); }
void Arm64Writer::put_sub_reg_reg_imm(int d, int l, u64 r) { if (w_) yam_arm64_writer_put_sub_reg_reg_imm(w_, (arm64_reg)d, (arm64_reg)l, r); }
void Arm64Writer::put_sub_reg_reg_reg(int d, int l, int r) { if (w_) yam_arm64_writer_put_sub_reg_reg_reg(w_, (arm64_reg)d, (arm64_reg)l, (arm64_reg)r); }
void Arm64Writer::put_and_reg_reg_imm(int d, int l, u64 r) { if (w_) yam_arm64_writer_put_and_reg_reg_imm(w_, (arm64_reg)d, (arm64_reg)l, r); }
void Arm64Writer::put_eor_reg_reg_reg(int d, int l, int r) { if (w_) yam_arm64_writer_put_eor_reg_reg_reg(w_, (arm64_reg)d, (arm64_reg)l, (arm64_reg)r); }
void Arm64Writer::put_cmp_reg_reg(int l, int r) { if (w_) yam_arm64_writer_put_cmp_reg_reg(w_, (arm64_reg)l, (arm64_reg)r); }
void Arm64Writer::put_tst_reg_imm(int r, u64 i) { if (w_) yam_arm64_writer_put_tst_reg_imm(w_, (arm64_reg)r, i); }
void Arm64Writer::put_ldr_reg_u64(int r, u64 v) { if (w_) yam_arm64_writer_put_ldr_reg_u64(w_, (arm64_reg)r, v); }
void Arm64Writer::put_ldr_reg_address(int r, void* a) { if (w_) yam_arm64_writer_put_ldr_reg_address(w_, (arm64_reg)r, reinterpret_cast<YamAddress>(a)); }
void Arm64Writer::put_ldr_reg_u64_ptr(int r, void* p) { if (w_) yam_arm64_writer_put_ldr_reg_u64_ptr(w_, (arm64_reg)r, reinterpret_cast<YamAddress>(p)); }
void Arm64Writer::put_ldr_reg_reg(int d, int s) { if (w_) yam_arm64_writer_put_ldr_reg_reg(w_, (arm64_reg)d, (arm64_reg)s); }
void Arm64Writer::put_ldr_reg_reg_offset(int d, int s, u32 o) { if (w_) yam_arm64_writer_put_ldr_reg_reg_offset(w_, (arm64_reg)d, (arm64_reg)s, o); }
void Arm64Writer::put_str_reg_reg(int s, int d) { if (w_) yam_arm64_writer_put_str_reg_reg(w_, (arm64_reg)s, (arm64_reg)d); }
void Arm64Writer::put_str_reg_reg_offset(int s, int d, u32 o) { if (w_) yam_arm64_writer_put_str_reg_reg_offset(w_, (arm64_reg)s, (arm64_reg)d, o); }
void Arm64Writer::put_ldp_reg_reg_reg_offset(int a, int b, int c, u32 o) { if (w_) yam_arm64_writer_put_ldp_reg_reg_reg_offset(w_, (arm64_reg)a, (arm64_reg)b, (arm64_reg)c, o, YAM_INDEX_SIGNED_OFFSET); }
void Arm64Writer::put_stp_reg_reg_reg_offset(int a, int b, int c, u32 o) { if (w_) yam_arm64_writer_put_stp_reg_reg_reg_offset(w_, (arm64_reg)a, (arm64_reg)b, (arm64_reg)c, o, YAM_INDEX_SIGNED_OFFSET); }
void Arm64Writer::put_adrp_reg_address(int r, void* a) { if (w_) yam_arm64_writer_put_adrp_reg_address(w_, (arm64_reg)r, reinterpret_cast<YamAddress>(a)); }
void Arm64Writer::put_b_imm(u64 i) { if (w_) yam_arm64_writer_put_b_imm(w_, i); }
void Arm64Writer::put_b_cond_imm(u32 c, u64 i) { (void)c; (void)i; }
void Arm64Writer::put_bl_imm(u64 i) { if (w_) yam_arm64_writer_put_bl_imm(w_, i); }
void Arm64Writer::put_blr_reg(int r) { if (w_) yam_arm64_writer_put_blr_reg(w_, (arm64_reg)r); }
void Arm64Writer::put_br_reg(int r) { if (w_) yam_arm64_writer_put_br_reg(w_, (arm64_reg)r); }
void Arm64Writer::put_branch_address(void* a) { if (w_) yam_arm64_writer_put_branch_address(w_, reinterpret_cast<YamAddress>(a)); }
void Arm64Writer::put_cbz_reg_imm(int r, u64 i) { if (w_) yam_arm64_writer_put_cbz_reg_imm(w_, (arm64_reg)r, i); }
void Arm64Writer::put_cbnz_reg_imm(int r, u64 i) { if (w_) yam_arm64_writer_put_cbnz_reg_imm(w_, (arm64_reg)r, i); }
void Arm64Writer::put_tbz_reg_imm_imm(int r, u32 b, u64 i) { if (w_) yam_arm64_writer_put_tbz_reg_imm_imm(w_, (arm64_reg)r, b, i); }
void Arm64Writer::put_tbnz_reg_imm_imm(int r, u32 b, u64 i) { if (w_) yam_arm64_writer_put_tbnz_reg_imm_imm(w_, (arm64_reg)r, b, i); }
void Arm64Writer::put_brk_imm(u16 i) { if (w_) yam_arm64_writer_put_brk_imm(w_, i); }
void Arm64Writer::put_bti() { if (w_) yam_arm64_writer_put_bti(w_); }
void Arm64Writer::put_svc_imm(u16 i) { if (w_) yam_arm64_writer_put_svc_imm(w_, i); }
void Arm64Writer::put_ubfm(int d, int s, u32 immr, u32 imms) { if (w_) yam_arm64_writer_put_ubfm(w_, (arm64_reg)d, (arm64_reg)s, imms, immr); }
void Arm64Writer::put_lsl_reg_imm(int d, int s, u32 sh) { if (w_) yam_arm64_writer_put_lsl_reg_imm(w_, (arm64_reg)d, (arm64_reg)s, sh); }
void Arm64Writer::put_lsr_reg_imm(int d, int s, u32 sh) { if (w_) yam_arm64_writer_put_lsr_reg_imm(w_, (arm64_reg)d, (arm64_reg)s, sh); }
void Arm64Writer::put_uxtw_reg_reg(int d, int s) { if (w_) yam_arm64_writer_put_uxtw_reg_reg(w_, (arm64_reg)d, (arm64_reg)s); }
void Arm64Writer::put_push_reg_reg(int a, int b) { if (w_) yam_arm64_writer_put_push_reg_reg(w_, (arm64_reg)a, (arm64_reg)b); }
void Arm64Writer::put_pop_reg_reg(int a, int b) { if (w_) yam_arm64_writer_put_pop_reg_reg(w_, (arm64_reg)a, (arm64_reg)b); }
void Arm64Writer::put_push_all_x() { if (w_) yam_arm64_writer_put_push_all_x_registers(w_); }
void Arm64Writer::put_pop_all_x() { if (w_) yam_arm64_writer_put_pop_all_x_registers(w_); }

// ---------------------------------------------------------------------------
// CodeSegment / CodeAllocator
// ---------------------------------------------------------------------------
CodeSegment::CodeSegment() : CodeSegment(Options{}) {}

CodeSegment::CodeSegment(const Options& o) {
    YamAddressSpec spec{};
    if (o.near) {
        spec.near_address = o.near;
        spec.max_distance = 0x7FFFFFFF;
    }
    s_ = yam_code_segment_new(o.code_size,
                               (o.near ? &spec : nullptr));
}
CodeSegment::~CodeSegment() { if (s_) { yam_code_segment_free(s_); s_ = nullptr; } }
void* CodeSegment::address() const { return s_ ? yam_code_segment_get_address(s_) : nullptr; }
usize CodeSegment::size() const { return s_ ? yam_code_segment_get_size(s_) : 0; }
usize CodeSegment::virtual_size() const { return s_ ? yam_code_segment_get_virtual_size(s_) : 0; }
void CodeSegment::realize() { if (s_) yam_code_segment_realize(s_); }
void CodeSegment::map(usize o, usize sz, void* a) { if (s_) yam_code_segment_map(s_, o, sz, a); }
bool CodeSegment::mark(void* code, usize size) {
    return yam_code_segment_mark(code, size, nullptr) != 0;
}

CodeAllocator::CodeAllocator(usize slice) {
    a_ = static_cast<YamCodeAllocator*>(yam_malloc(sizeof(YamCodeAllocator)));
    if (a_) yam_code_allocator_init(a_, slice);
}
CodeAllocator::~CodeAllocator() {
    if (a_) { yam_code_allocator_free(a_); yam_free(a_); a_ = nullptr; }
}
Result<void*> CodeAllocator::alloc_slice() {
    if (!a_) return Result<void*>::err(ErrorCode::BackendUnavailable, "ca");
    YamCodeSlice* s = yam_code_allocator_alloc_slice(a_);
    return s ? Result<void*>::ok(s) : Result<void*>::err(ErrorCode::MemoryAllocFailed, "slice");
}
Result<void*> CodeAllocator::try_alloc_slice_near(void* near, usize dist, usize align) {
    if (!a_) return Result<void*>::err(ErrorCode::BackendUnavailable, "ca");
    YamAddressSpec spec{};
    spec.near_address = near;
    spec.max_distance = dist;
    YamCodeSlice* s = yam_code_allocator_try_alloc_slice_near(a_, &spec, align);
    return s ? Result<void*>::ok(s) : Result<void*>::err(ErrorCode::MemoryAllocFailed, "near");
}
void CodeAllocator::commit() { if (a_) yam_code_allocator_commit(a_); }

// ---------------------------------------------------------------------------
// ElfModule
// ---------------------------------------------------------------------------
ElfModule::~ElfModule() { if (m_) { yam_object_unref(m_); m_ = nullptr; } }

Result<ElfModule> ElfModule::from_file(const String& path) {
    YamElfModule* m = yam_elf_module_new_from_file(path.c_str(), nullptr);
    if (!m) return Result<ElfModule>::err(ErrorCode::SymbolNotFound, path);
    ElfModule out; out.m_ = m;
    return Result<ElfModule>::ok(std::move(out));
}
Result<ElfModule> ElfModule::from_blob(const void* blob, usize size) {
    GBytes* b = g_bytes_new(blob, size);
    YamElfModule* m = yam_elf_module_new_from_blob(b, nullptr);
    if (b) g_bytes_unref(b);
    if (!m) return Result<ElfModule>::err(ErrorCode::SymbolNotFound, "blob");
    ElfModule out; out.m_ = m;
    return Result<ElfModule>::ok(std::move(out));
}
Result<ElfModule> ElfModule::from_memory(const String& path, void* base) {
    YamElfModule* m = yam_elf_module_new_from_memory(path.c_str(),
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(base)), nullptr);
    if (!m) return Result<ElfModule>::err(ErrorCode::SymbolNotFound, "mem");
    ElfModule out; out.m_ = m;
    return Result<ElfModule>::ok(std::move(out));
}
Result<void> ElfModule::load() {
    if (!m_) return Result<void>::err(ErrorCode::BackendUnavailable, "elf");
    return yam_elf_module_load(m_, nullptr) ? Result<void>::ok()
        : Result<void>::err(ErrorCode::InternalError, "load");
}
String ElfModule::source_path() const {
    if (!m_) return {};
    const gchar* s = yam_elf_module_get_source_path(m_);
    return s ? s : "";
}
void* ElfModule::base_address() const {
    if (!m_) return nullptr;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(yam_elf_module_get_base_address(m_)));
}
usize ElfModule::mapped_size() const { return m_ ? (usize)yam_elf_module_get_mapped_size(m_) : 0; }
void* ElfModule::entrypoint() const {
    if (!m_) return nullptr;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(yam_elf_module_get_entrypoint(m_)));
}
u32 ElfModule::pointer_size() const { return m_ ? yam_elf_module_get_pointer_size(m_) : 8; }
String ElfModule::interpreter() const {
    if (!m_) return {};
    const gchar* s = yam_elf_module_get_interpreter(m_);
    return s ? s : "";
}

std::vector<ElfSectionDetails> ElfModule::sections() const {
    return {};
}
std::vector<ElfSymbolDetails> ElfModule::symbols() const {
    return {};
}
std::vector<ElfSegmentDetails> ElfModule::segments() const {
    return {};
}
std::vector<String> ElfModule::dependencies() const {
    return {};
}

// ---------------------------------------------------------------------------
// MetalArray / MetalHashTable
// ---------------------------------------------------------------------------
MetalArray::MetalArray() {
    arr_ = yam_malloc(sizeof(YamMetalArray));
    if (arr_) yam_metal_array_init(static_cast<YamMetalArray*>(arr_), sizeof(void*));
}
MetalArray::~MetalArray() {
    if (arr_) { yam_metal_array_free(static_cast<YamMetalArray*>(arr_)); yam_free(arr_); arr_ = nullptr; }
}
void MetalArray::init(usize sz) { if (arr_) yam_metal_array_init(static_cast<YamMetalArray*>(arr_), (guint)sz); }
void* MetalArray::at(u32 i) { return arr_ ? yam_metal_array_element_at(static_cast<YamMetalArray*>(arr_), i) : nullptr; }
void* MetalArray::append() { return arr_ ? yam_metal_array_append(static_cast<YamMetalArray*>(arr_)) : nullptr; }
void* MetalArray::insert_at(u32 i) { return arr_ ? yam_metal_array_insert_at(static_cast<YamMetalArray*>(arr_), i) : nullptr; }
void MetalArray::remove_at(u32 i) { if (arr_) yam_metal_array_remove_at(static_cast<YamMetalArray*>(arr_), i); }
void MetalArray::remove_all() { if (arr_) yam_metal_array_remove_all(static_cast<YamMetalArray*>(arr_)); }
void MetalArray::ensure_capacity(u32 c) { if (arr_) yam_metal_array_ensure_capacity(static_cast<YamMetalArray*>(arr_), c); }
void MetalArray::extents(void** s, void** e) { if (arr_) yam_metal_array_get_extents(static_cast<YamMetalArray*>(arr_), s, e); }

MetalHashTable::MetalHashTable() {
    t_ = yam_metal_hash_table_new(nullptr, nullptr);
}
MetalHashTable::~MetalHashTable() {
    if (t_) { yam_metal_hash_table_destroy(t_); t_ = nullptr; }
}
bool MetalHashTable::insert(void* k, void* v) { return t_ && yam_metal_hash_table_insert(t_, k, v); }
bool MetalHashTable::replace(void* k, void* v) { return t_ && yam_metal_hash_table_replace(t_, k, v); }
bool MetalHashTable::remove(const void* k) { return t_ && yam_metal_hash_table_remove(t_, k); }
void MetalHashTable::remove_all() { if (t_) yam_metal_hash_table_remove_all(t_); }
void* MetalHashTable::lookup(const void* k) const { return t_ ? yam_metal_hash_table_lookup(t_, k) : nullptr; }
bool MetalHashTable::contains(const void* k) const { return t_ && yam_metal_hash_table_contains(t_, k); }
u32 MetalHashTable::size() const { return t_ ? yam_metal_hash_table_size(t_) : 0; }

} // namespace yam
