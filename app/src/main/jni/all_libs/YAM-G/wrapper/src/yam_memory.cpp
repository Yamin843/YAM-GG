// ===========================================================================
// yam_memory.cpp — Memory + Module + Symbol + Process
// Uses the real YAMJS.h signatures:
//   yam_memory_read  (const void*, gsize, gsize*)  -> guint8*
//   yam_memory_write (void*, const guint8*, gsize) -> gboolean
//   yam_memory_allocate (void*, gsize, gsize, YamPageProtection)
//   yam_memory_allocate_near (const YamAddressSpec*, gsize, gsize, YamPageProtection)
//   yam_memory_free (void*, gsize)
//   yam_memory_query_protection (const void*, YamPageProtection*)
//   yam_memory_query_region (const void*, YamMemoryRange*, YamPageProtection*)
//   yam_memory_is_readable (const void*, gsize)
//   yam_memory_scan (const YamMemoryRange*, const YamMatchPattern*,
//                    YamMemoryScanMatchFunc, gpointer)
//   yam_memory_mark_code (void*, gsize)
//
// Note: no yam_memory_protect exists in the library. Memory::protect uses
// POSIX mprotect directly.
// ===========================================================================

#include "yam.hpp"
#include "yam_internal.hpp"

#include <dlfcn.h>
#include <cxxabi.h>
#include <sys/mman.h>
#include <unistd.h>

namespace yam {

namespace {
std::mutex g_alloc_mu;
std::unordered_map<void*, usize> g_alloc_sizes;

void track_alloc(void* p, usize s) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(g_alloc_mu);
    g_alloc_sizes[p] = s;
}

bool take_alloc(void* p, usize& out_size) {
    if (!p) return false;
    std::lock_guard<std::mutex> lk(g_alloc_mu);
    auto it = g_alloc_sizes.find(p);
    if (it == g_alloc_sizes.end()) return false;
    out_size = it->second;
    g_alloc_sizes.erase(it);
    return true;
}
} // namespace


// ===========================================================================
// Protection <-> YamPageProtection
// ===========================================================================

namespace {

YamPageProtection to_yam_prot(Protection p) {
    guint r = 0;
    if (has_flag(p, Protection::Read))  r |= YAM_PAGE_READ;
    if (has_flag(p, Protection::Write)) r |= YAM_PAGE_WRITE;
    if (has_flag(p, Protection::Exec))  r |= YAM_PAGE_EXECUTE;
    return static_cast<YamPageProtection>(r);
}

Protection from_yam_prot(YamPageProtection p) {
    u32 r = 0;
    if (p & YAM_PAGE_READ)    r |= static_cast<u32>(Protection::Read);
    if (p & YAM_PAGE_WRITE)   r |= static_cast<u32>(Protection::Write);
    if (p & YAM_PAGE_EXECUTE) r |= static_cast<u32>(Protection::Exec);
    return static_cast<Protection>(r);
}

int to_posix_prot(Protection p) {
    int r = 0;
    if (has_flag(p, Protection::Read))  r |= PROT_READ;
    if (has_flag(p, Protection::Write)) r |= PROT_WRITE;
    if (has_flag(p, Protection::Exec))  r |= PROT_EXEC;
    return r;
}

} // namespace

// ===========================================================================
// Memory::protect — POSIX mprotect
// ===========================================================================

Result<void> Memory::protect(void* addr, usize size, Protection prot) {
    if (!addr || !size)
        return Result<void>::err(ErrorCode::InvalidArgument, "protect");

    long page = ::sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;

    uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    uintptr_t start = a & ~(static_cast<uintptr_t>(page) - 1);
    usize span = size + (a - start);

    if (::mprotect(reinterpret_cast<void*>(start), span,
                   to_posix_prot(prot)) != 0) {
        return Result<void>::err(ErrorCode::MemoryAccessDenied, "mprotect");
    }
    return Result<void>::ok();
}

// ===========================================================================
// Memory::query — yam_memory_query_protection
// ===========================================================================

Result<Protection> Memory::query(void* addr) {
    if (!addr) return Result<Protection>::err(ErrorCode::InvalidArgument, "query");
    YamPageProtection yp = YAM_PAGE_NO_ACCESS;
    if (!yam_memory_query_protection(addr, &yp)) {
        return Result<Protection>::err(ErrorCode::MemoryAccessDenied, "query");
    }
    return Result<Protection>::ok(from_yam_prot(yp));
}

// ===========================================================================
// Memory::alloc — yam_memory_allocate(addr, size, alignment, prot)
// ===========================================================================

Result<void*> Memory::alloc(usize size) {
    if (!size) return Result<void*>::err(ErrorCode::InvalidArgument, "size=0");
    void* p = yam_memory_allocate(nullptr, size, 0,
                                   static_cast<YamPageProtection>(
                                       YAM_PAGE_READ | YAM_PAGE_WRITE | YAM_PAGE_EXECUTE));
    if (!p) return Result<void*>::err(ErrorCode::MemoryAllocFailed, "allocate");
    track_alloc(p, size);
    return Result<void*>::ok(p);
}

Result<void*> Memory::alloc(usize size, Protection prot) {
    if (!size) return Result<void*>::err(ErrorCode::InvalidArgument, "size=0");
    void* p = yam_memory_allocate(nullptr, size, 0, to_yam_prot(prot));
    if (!p) return Result<void*>::err(ErrorCode::MemoryAllocFailed, "allocate");
    track_alloc(p, size);
    return Result<void*>::ok(p);
}

Result<void*> Memory::alloc_near(void* near, usize size) {
    if (!size) return Result<void*>::err(ErrorCode::InvalidArgument, "size=0");
    if (!near) return alloc(size);
    YamAddressSpec spec{};
    spec.near_address = near;
    spec.max_distance = 0x7FFFFFFF;  // +-2GB
    void* p = yam_memory_allocate_near(&spec, size, 0,
                                        static_cast<YamPageProtection>(
                                            YAM_PAGE_READ | YAM_PAGE_WRITE | YAM_PAGE_EXECUTE));
    if (!p) return Result<void*>::err(ErrorCode::MemoryAllocFailed, "allocate_near");
    track_alloc(p, size);
    return Result<void*>::ok(p);
}

// ===========================================================================
// Memory::free — yam_memory_free(address, size)
// Problem: yam_memory_free requires size. We track allocation sizes.
// ===========================================================================

// ملاحظة: نعدل المسار داخل alloc/alloc_near لتسجيل الحجم
// (لا نستطيع ذلك من خارج الدالة، لذلك نعيد كتابة alloc لتستدعي track_alloc)

Result<void> Memory::free(void* addr) {
    if (!addr) return Result<void>::ok();
    usize size = 0;
    if (!take_alloc(addr, size)) {
        // لم يُسجَّل — لا نعرف الحجم. نعود بخطأ بدل crash.
        return Result<void>::err(ErrorCode::InvalidArgument, "unknown alloc");
    }
    if (!yam_memory_free(addr, size)) {
        return Result<void>::err(ErrorCode::MemoryAccessDenied, "free");
    }
    return Result<void>::ok();
}

// ===========================================================================
// Memory::copy
// ===========================================================================

Result<void> Memory::copy(void* dst, const void* src, usize size) {
    if (!dst || !src) return Result<void>::err(ErrorCode::InvalidArgument, "copy");
    std::memmove(dst, src, size);
    return Result<void>::ok();
}

// ===========================================================================
// Memory::read — yam_memory_read(address, len, &n_bytes)
// ===========================================================================

Result<void> Memory::read(void* addr, void* out, usize size) {
    if (!addr || !out) return Result<void>::err(ErrorCode::InvalidArgument, "read");
    gsize got = 0;
    guint8* p = yam_memory_read(addr, size, &got);
    if (!p || got != size) {
        if (p) yam_free(p);
        return Result<void>::err(ErrorCode::MemoryReadFailed, "read");
    }
    std::memcpy(out, p, size);
    yam_free(p);
    return Result<void>::ok();
}

Result<void> Memory::write(void* addr, const void* in, usize size) {
    if (!addr || !in) return Result<void>::err(ErrorCode::InvalidArgument, "write");
    if (!yam_memory_write(addr, static_cast<const guint8*>(in), size)) {
        return Result<void>::err(ErrorCode::MemoryWriteFailed, "write");
    }
    return Result<void>::ok();
}

Result<ByteVector> Memory::read_bytes(void* addr, usize size) {
    if (!addr) return Result<ByteVector>::err(ErrorCode::InvalidArgument, "null");
    gsize got = 0;
    guint8* p = yam_memory_read(addr, size, &got);
    if (!p || got != size) {
        if (p) yam_free(p);
        return Result<ByteVector>::err(ErrorCode::MemoryReadFailed, "read_bytes");
    }
    ByteVector out(p, p + size);
    yam_free(p);
    return Result<ByteVector>::ok(std::move(out));
}

Result<void> Memory::write_bytes(void* addr, const ByteVector& d) {
    if (!addr) return Result<void>::err(ErrorCode::InvalidArgument, "null");
    if (!yam_memory_write(addr, d.data(), d.size())) {
        return Result<void>::err(ErrorCode::MemoryWriteFailed, "write_bytes");
    }
    return Result<void>::ok();
}

// ===========================================================================
// Typed read / write
// ===========================================================================

#define YAM_MEM_R(T, sfx)                                                    \
Result<T> Memory::read_##sfx(void* a) {                                      \
    if (!a) return Result<T>::err(ErrorCode::InvalidArgument, "null");       \
    T v;                                                                     \
    if (!yam_memory_read(a, sizeof(v), nullptr))                             \
        return Result<T>::err(ErrorCode::MemoryReadFailed, "read");          \
    std::memcpy(&v, a, sizeof(v));                                           \
    return Result<T>::ok(v);                                                 \
}

YAM_MEM_R(u8,u8) YAM_MEM_R(u16,u16) YAM_MEM_R(u32,u32) YAM_MEM_R(u64,u64)
YAM_MEM_R(i8,i8) YAM_MEM_R(i16,i16) YAM_MEM_R(i32,i32) YAM_MEM_R(i64,i64)
YAM_MEM_R(f32,f32) YAM_MEM_R(f64,f64)

Result<void*> Memory::read_ptr(void* a) {
    if (!a) return Result<void*>::err(ErrorCode::InvalidArgument, "null");
    void* v = nullptr;
    if (!yam_memory_read(a, sizeof(v), nullptr))
        return Result<void*>::err(ErrorCode::MemoryReadFailed, "read_ptr");
    std::memcpy(&v, a, sizeof(v));
    return Result<void*>::ok(v);
}

#define YAM_MEM_W(T, sfx)                                                    \
Result<void> Memory::write_##sfx(void* a, T v) {                             \
    if (!a) return Result<void>::err(ErrorCode::InvalidArgument, "null");    \
    if (!yam_memory_write(a, reinterpret_cast<const guint8*>(&v), sizeof(v)))\
        return Result<void>::err(ErrorCode::MemoryWriteFailed, "write");     \
    return Result<void>::ok();                                               \
}

YAM_MEM_W(u8,u8) YAM_MEM_W(u16,u16) YAM_MEM_W(u32,u32) YAM_MEM_W(u64,u64)
YAM_MEM_W(i8,i8) YAM_MEM_W(i16,i16) YAM_MEM_W(i32,i32) YAM_MEM_W(i64,i64)
YAM_MEM_W(f32,f32) YAM_MEM_W(f64,f64)

Result<void> Memory::write_ptr(void* a, void* v) {
    if (!a) return Result<void>::err(ErrorCode::InvalidArgument, "null");
    if (!yam_memory_write(a, reinterpret_cast<const guint8*>(&v), sizeof(v)))
        return Result<void>::err(ErrorCode::MemoryWriteFailed, "write_ptr");
    return Result<void>::ok();
}

// ===========================================================================
// Strings
// ===========================================================================

String Memory::read_cstring(void* addr, usize max) {
    if (!addr) return {};
    const char* p = static_cast<const char*>(addr);
    usize n = 0;
    while (n < max) {
        char c = 0;
        if (!yam_memory_read(p + n, 1, nullptr)) break;
        std::memcpy(&c, p + n, 1);
        if (c == 0) break;
        ++n;
    }
    auto r = read_bytes(addr, n);
    if (!r) return {};
    return String(reinterpret_cast<const char*>(r.value().data()),
                   r.value().size());
}

String Memory::read_utf8(void* addr, usize size) {
    auto r = read_bytes(addr, size);
    if (!r) return {};
    return String(reinterpret_cast<const char*>(r.value().data()),
                   r.value().size());
}

String Memory::read_utf16(void* addr, usize chars) {
    auto r = read_bytes(addr, chars * 2);
    if (!r || r.value().size() < 2) return {};
    const u16* p = reinterpret_cast<const u16*>(r.value().data());
    String out; out.reserve(chars);
    for (usize i = 0; i < chars; ++i) {
        u16 c = p[i];
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

Result<void> Memory::write_utf8(void* addr, const String& s) {
    return write_bytes(addr, ByteVector(s.begin(), s.end()));
}

// ===========================================================================
// Scan — yam_memory_scan(range, pattern, func, user_data)
// ===========================================================================

namespace {
struct ScanCtx {
    std::vector<void*>* out;
};

gboolean scan_visitor(YamAddress address, gsize /*size*/, gpointer user) {
    auto* ctx = static_cast<ScanCtx*>(user);
    ctx->out->push_back(reinterpret_cast<void*>(address));
    return 1;  // continue
}
} // namespace

Result<std::vector<void*>> Memory::scan(void* base, usize size, const String& pattern) {
    std::vector<void*> out;
    if (!base || !size || pattern.empty())
        return Result<std::vector<void*>>::ok(out);

    YamMatchPattern* mp = yam_match_pattern_new_from_string(pattern.c_str());
    if (!mp)
        return Result<std::vector<void*>>::err(ErrorCode::InvalidArgument,
                                                "bad pattern: " + pattern);

    YamMemoryRange range{};
    range.base_address = reinterpret_cast<YamAddress>(base);
    range.size = size;

    ScanCtx ctx{ &out };
    yam_memory_scan(&range, mp, scan_visitor, &ctx);
    yam_match_pattern_unref(mp);
    return Result<std::vector<void*>>::ok(std::move(out));
}

Result<void*> Memory::scan_first(void* base, usize size, const String& pattern) {
    auto r = scan(base, size, pattern);
    if (!r) return Result<void*>::err(r.error_code(), r.error_message());
    return Result<void*>::ok(r.value().empty() ? nullptr : r.value().front());
}

Result<std::vector<void*>> Memory::scan_module(const String& mod, const String& pat) {
    auto m = Module::find(mod);
    if (!m) return Result<std::vector<void*>>::err(m.error_code(), m.error_message());
    return scan(m.value().base(), m.value().size(), pat);
}

// ===========================================================================
// Queries
// ===========================================================================

bool Memory::is_readable(void* addr, usize size) {
    if (!addr) return false;
    return yam_memory_is_readable(addr, size) != 0;
}

bool Memory::is_writable(void* addr, usize) {
    auto q = query(addr);
    return q && has_flag(q.value(), Protection::Write);
}

bool Memory::is_executable(void* addr, usize) {
    auto q = query(addr);
    return q && has_flag(q.value(), Protection::Exec);
}

// ===========================================================================
// MemoryPatch — install/uninstall via mprotect + memcpy
// ===========================================================================

MemoryPatch::MemoryPatch() = default;

MemoryPatch::~MemoryPatch() {
    if (installed_) { try { uninstall(); } catch (...) {} }
}

Result<void> MemoryPatch::install(void* addr, const ByteVector& code) {
    if (!addr || code.empty())
        return Result<void>::err(ErrorCode::InvalidArgument, "install");
    if (installed_)
        return Result<void>::err(ErrorCode::InvalidArgument, "already installed");

    addr_ = addr;

    // احفظ الأصل
    auto orig = Memory::read_bytes(addr, code.size());
    if (!orig) return Result<void>::err(orig.error_code(), orig.error_message());
    original_ = std::move(orig.value());

    // اجعل الصفحة قابلة للكتابة
    auto pr = Memory::protect(addr, code.size(), Protection::RWX);
    if (!pr) return pr;

    std::memcpy(addr, code.data(), code.size());

    // أعد الحماية إلى RX (أو الأصل إن أمكن)
    Memory::protect(addr, code.size(), Protection::RX);

    installed_ = true;
    return Result<void>::ok();
}

Result<void> MemoryPatch::install(void* addr, const String& hex) {
    auto bytes = parse_hex_code(hex);
    if (bytes.empty())
        return Result<void>::err(ErrorCode::InvalidArgument, "bad hex");
    return install(addr, bytes);
}

Result<void> MemoryPatch::uninstall() {
    if (!installed_ || !addr_) return Result<void>::ok();

    auto pr = Memory::protect(addr_, original_.size(), Protection::RWX);
    if (!pr) return pr;

    std::memcpy(addr_, original_.data(), original_.size());

    Memory::protect(addr_, original_.size(), Protection::RX);

    installed_ = false;
    return Result<void>::ok();
}

// ===========================================================================
// parse_hex_code
// ===========================================================================

ByteVector parse_hex_code(const String& hex) {
    ByteVector out;
    int hi = -1;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (char c : hex) {
        int v = nib(c);
        if (v < 0) continue;
        if (hi < 0) hi = v;
        else {
            out.push_back(static_cast<u8>((hi << 4) | v));
            hi = -1;
        }
    }
    return out;
}

// ===========================================================================
// Module
// ===========================================================================

gboolean Module::visit_module(YamModule* m, gpointer user) {
    auto* out = static_cast<std::vector<Module>*>(user);
    if (!m || !out) return 0;
    Module mod;
    const gchar* nm = yam_module_get_name(m);
    const gchar* pt = yam_module_get_path(m);
    mod.name_ = nm ? nm : "";
    mod.path_ = pt ? pt : "";
    const YamMemoryRange* range = yam_module_get_range(m);
    if (range) {
        mod.base_ = reinterpret_cast<void*>(static_cast<uintptr_t>(range->base_address));
        mod.size_ = range->size;
    }
    out->push_back(std::move(mod));
    return 1;
}

std::vector<Module> Module::enumerate() {
    std::vector<Module> out;
    YamModuleRegistry* reg = yam_module_registry_obtain();
    if (!reg) return out;
    yam_module_registry_enumerate_modules(reg, Module::visit_module, &out);
    return out;
}

Result<Module> Module::find(const String& name) {
    YamModule* m = yam_process_find_module_by_name(name.c_str());
    if (!m) return Result<Module>::err(ErrorCode::ModuleNotFound, name);
    Module mod;
    const gchar* nm = yam_module_get_name(m);
    const gchar* pt = yam_module_get_path(m);
    mod.name_ = nm ? nm : name;
    mod.path_ = pt ? pt : "";
    const YamMemoryRange* range = yam_module_get_range(m);
    if (range) {
        mod.base_ = reinterpret_cast<void*>(static_cast<uintptr_t>(range->base_address));
        mod.size_ = range->size;
    }
    return Result<Module>::ok(std::move(mod));
}

Result<Module> Module::find_by_address(void* addr) {
    YamModule* m = yam_process_find_module_by_address(
        static_cast<YamAddress>(reinterpret_cast<uintptr_t>(addr)));
    if (!m) return Result<Module>::err(ErrorCode::ModuleNotFound, "addr");
    Module mod;
    const gchar* nm = yam_module_get_name(m);
    const gchar* pt = yam_module_get_path(m);
    mod.name_ = nm ? nm : "";
    mod.path_ = pt ? pt : "";
    const YamMemoryRange* range = yam_module_get_range(m);
    if (range) {
        mod.base_ = reinterpret_cast<void*>(static_cast<uintptr_t>(range->base_address));
        mod.size_ = range->size;
    }
    return Result<Module>::ok(std::move(mod));
}

Module Module::main() {
    YamModule* m = yam_process_get_main_module();
    Module mod;
    if (m) {
        const gchar* nm = yam_module_get_name(m);
        const gchar* pt = yam_module_get_path(m);
        mod.name_ = nm ? nm : "";
        mod.path_ = pt ? pt : "";
        const YamMemoryRange* range = yam_module_get_range(m);
        if (range) {
            mod.base_ = reinterpret_cast<void*>(static_cast<uintptr_t>(range->base_address));
            mod.size_ = range->size;
        }
    }
    return mod;
}

void* Module::find_export(const String& name) const {
    if (name.empty()) return nullptr;
    YamModule* m = yam_process_find_module_by_name(name_.c_str());
    if (!m) return nullptr;
    YamAddress a = yam_module_find_export_by_name(m, name.c_str());
    return reinterpret_cast<void*>(static_cast<uintptr_t>(a));
}

void* Module::find_symbol(const String& name) const {
    if (name.empty()) return nullptr;
    YamModule* m = yam_process_find_module_by_name(name_.c_str());
    if (!m) return nullptr;
    YamAddress a = yam_module_find_symbol_by_name(m, name.c_str());
    return reinterpret_cast<void*>(static_cast<uintptr_t>(a));
}

void* Module::find_global_export(const String& name) {
    if (name.empty()) return nullptr;
    return yam_module_find_global_export_by_name(name.c_str());
}

namespace {
struct ExportCtx { std::vector<ExportSymbol>* out; };
gboolean export_visitor(const YamExportDetails* d, gpointer user) {
    auto* ctx = static_cast<ExportCtx*>(user);
    if (!d || !ctx || !ctx->out) return 0;
    ExportSymbol e;
    e.name = d->name ? d->name : "";
    e.address = reinterpret_cast<void*>(static_cast<uintptr_t>(d->address));
    e.type = std::to_string(static_cast<int>(d->type));
    ctx->out->push_back(std::move(e));
    return 1;
}
}

std::vector<ExportSymbol> Module::exports() const {
    std::vector<ExportSymbol> out;
    YamModule* m = yam_process_find_module_by_name(name_.c_str());
    if (!m) return out;
    ExportCtx ctx{ &out };
    yam_module_enumerate_exports(m, export_visitor, &ctx);
    return out;
}

std::vector<ImportSymbol> Module::imports() const { return {}; }

std::vector<ModuleRange> Module::ranges() const {
    std::vector<ModuleRange> out;
    auto maps = detail::ProcMaps::read();
    auto b = reinterpret_cast<u64>(base_);
    for (auto& m : maps) {
        if (m.start >= b && m.end <= b + size_) {
            ModuleRange r;
            r.base = reinterpret_cast<void*>(static_cast<uintptr_t>(m.start));
            r.size = m.end - m.start;
            r.protection = m.prot;
            r.file = m.path;
            out.push_back(std::move(r));
        }
    }
    return out;
}

// ===========================================================================
// Symbol
// ===========================================================================

void* Symbol::resolve(const String& name) {
    if (name.empty()) return nullptr;
    return yam_find_function(name.c_str());
}

void* Symbol::resolve_in(const String& mod, const String& name) {
    auto m = Module::find(mod);
    if (!m) return nullptr;
    return m.value().find_export(name);
}

std::vector<void*> Symbol::resolve_matching(const String& pattern) {
    std::vector<void*> out;
    if (pattern.empty()) return out;
    GArray* arr = yam_find_functions_matching(pattern.c_str());
    if (!arr) return out;
    for (guint i = 0; i < arr->len; ++i) {
        YamAddress a = g_array_index(arr, YamAddress, i);
        out.push_back(reinterpret_cast<void*>(static_cast<uintptr_t>(a)));
    }
    g_array_unref(arr);
    return out;
}

String Symbol::demangle(const String& mangled) {
    int status = 0;
    char* out = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
    if (status != 0 || !out) return mangled;
    String s(out);
    std::free(out);
    return s;
}

String Symbol::to_string(void* addr) {
    YamReturnAddressDetails d{};
    if (yam_return_address_details_from_address(addr, &d)) {
        if (d.function_name[0]) return demangle(d.function_name);
    }
    Dl_info info{};
    if (dladdr(addr, &info) && info.dli_sname) return demangle(info.dli_sname);
    return {};
}

} // namespace yam
