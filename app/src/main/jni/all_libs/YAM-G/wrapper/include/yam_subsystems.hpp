#ifndef YAM_SUBSYSTEMS_HPP
#define YAM_SUBSYSTEMS_HPP

// ===========================================================================
// yam_subsystems.hpp — كل الأقسام، لا حذف
// ===========================================================================

#include "yam.hpp"
#include "yam_wrapper.h"

namespace yam {

// ===========================================================================
// CModule
// ===========================================================================
class CModule {
public:
    CModule() = default;
    CModule(const String& source, const String& name = "",
            void* near = nullptr, usize max_distance = 0);
    ~CModule();
    YAM_NONCOPYABLE(CModule);
    YAM_DEFAULT_MOVABLE(CModule);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    YAM_NODISCARD YamCModule* raw() const noexcept { return raw_; }
    Result<void> link();
    YAM_NODISCARD void* symbol(const String& name) const;
    YAM_NODISCARD YamMemoryRange range() const;
    void add_symbol(const String& name, void* value);
    std::vector<std::pair<String, void*>> symbols() const;
    void drop_metadata();
private:
    YamCModule* raw_{nullptr};
};

// ===========================================================================
// Cloak
// ===========================================================================
class Cloak {
public:
    struct MemoryRange { void* base; usize size; };
    static bool has_range_containing(void* address);
    static void add_range(void* base, usize size);
    static void remove_range(void* base, usize size);
    static void clip_range(void*& base, usize& size);
    static void add_thread(u32 tid);
    static void remove_thread(u32 tid);
    static bool has_thread(u32 tid);
    static void add_fd(i32 fd);
    static void remove_fd(i32 fd);
    static bool has_fd(i32 fd);
    static bool is_locked();
    static std::vector<MemoryRange> enumerate_ranges();
    static std::vector<u32> enumerate_threads();
    static std::vector<i32> enumerate_fds();
};

// ===========================================================================
// BoundsChecker
// ===========================================================================
class BoundsChecker {
public:
    BoundsChecker();
    ~BoundsChecker();
    YAM_NONCOPYABLE(BoundsChecker);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    Result<void> attach(const String& apis = "");
    Result<void> detach();
    void set_front_alignment(u32 g);
    void set_pool_size(u32 size);
    YAM_NODISCARD u32 front_alignment() const;
    YAM_NODISCARD u32 pool_size() const;
private:
    YamBoundsChecker* raw_{nullptr};
};

// ===========================================================================
// AllocatorProbe
// ===========================================================================
class AllocatorProbe {
public:
    AllocatorProbe();
    ~AllocatorProbe();
    YAM_NONCOPYABLE(AllocatorProbe);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    Result<void> attach(const String& apis = "");
    Result<void> detach();
    void suppress();
private:
    YamAllocatorProbe* raw_{nullptr};
};

// ===========================================================================
// ApiResolver
// ===========================================================================
struct ApiDetails {
    void*  address{nullptr};
    String name;
    i64    size{0};
};

class ApiResolver {
public:
    enum class Type { Default = 0, Module = 1, Swift = 2, Objc = 3 };
    ApiResolver() = default;
    explicit ApiResolver(Type t);
    ~ApiResolver();
    YAM_NONCOPYABLE(ApiResolver);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    Result<std::vector<ApiDetails>> enumerate_matches(const String& query);
private:
    YamApiResolver* raw_{nullptr};
};

// ===========================================================================
// SourceMap
// ===========================================================================
class SourceMap {
public:
    SourceMap() = default;
    explicit SourceMap(const String& json);
    ~SourceMap();
    YAM_NONCOPYABLE(SourceMap);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    struct Position { u32 line; u32 column; String name; String source; };
    Result<Position> resolve(u32 line, u32 column);
private:
    YamSourceMap* raw_{nullptr};
};

// ===========================================================================
// TlsKey
// ===========================================================================
class TlsKey {
public:
    TlsKey();
    ~TlsKey();
    YAM_NONCOPYABLE(TlsKey);
    YAM_DEFAULT_MOVABLE(TlsKey);
    YAM_NODISCARD bool valid() const noexcept { return key_ != 0; }
    void* get() const;
    void  set(void* value);
private:
    YamTlsKey key_{0};
};

// ===========================================================================
// EventSink — callback 3 args
// ===========================================================================
struct SinkEvent { u32 type{0}; };

class EventSink {
public:
    using Callback = std::function<void(const SinkEvent&)>;
    EventSink();
    explicit EventSink(Callback cb);
    ~EventSink();
    YAM_NONCOPYABLE(EventSink);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    YAM_NODISCARD YamEventSink* raw() const noexcept { return raw_; }
    void start();
    void stop();
    void flush();
    u32  query_mask() const;
private:
    YamEventSink* raw_{nullptr};
    Callback cb_;
};

// ===========================================================================
// Exceptor
// ===========================================================================
struct ExceptionDetails {
    u64    thread_id{0};
    u32    type{0};
    void*  address{nullptr};
    String type_name;
};

class Exceptor {
public:
    using Handler = std::function<bool(const ExceptionDetails&)>;
    Exceptor();
    ~Exceptor();
    YAM_NONCOPYABLE(Exceptor);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    Result<void> add(Handler h);
    Result<void> remove();
    Result<void> set_mode(i32 mode);
    Result<void> reset();
    YAM_NODISCARD bool has_scope() const;
    void dispatch(const ExceptionDetails& ed);
private:
    YamExceptor* raw_{nullptr};
    std::unordered_map<void*, Handler> handlers_;
    std::mutex mu_;
};

// ===========================================================================
// Kernel
// ===========================================================================
class Kernel {
public:
    static bool available();
    static void* alloc_pages(u32 n);
    static void  free_pages(void* p);
    static usize page_size();
    static bool read(void* addr, void* out, usize size);
    static bool write(void* addr, const void* in, usize size);
    static bool mprotect(void* addr, usize size, Protection prot);
    static void* base_address();
    static void  set_base_address(void* addr);
    static std::vector<String> enumerate_modules();
};

// ===========================================================================
// ThreadRegistry — YamThreadDetails ظاهر، نقرأ الحقول مباشرة
// ===========================================================================
struct ThreadInfo {
    u32    id{0};
    String name;
    u32    flags{0};
    u32    state{0};
};

class ThreadRegistry {
public:
    static std::vector<ThreadInfo> enumerate();
    static Result<ThreadInfo> find(u32 tid);
    static Result<std::vector<std::pair<void*, usize>>> ranges(u32 tid);
};

// ===========================================================================
// Linux — أعيد بناؤها على yam_process_* + yam_thread_*
// ===========================================================================
class Linux {
public:
    struct PthreadSpec { u32 tid{0}; String name; u32 state{0}; };
    static std::vector<PthreadSpec> enumerate_pthreads();
    static Result<PthreadSpec> find(u32 tid);
    static bool suspend_thread(u32 tid);
    static bool resume_thread(u32 tid);
    static bool set_hardware_breakpoint(u32 tid, u32 bp_id, void* addr);
    static bool unset_hardware_breakpoint(u32 tid, u32 bp_id);
    static bool set_hardware_watchpoint(u32 tid, u32 wp_id, void* addr, u32 size, u32 cond);
    static bool unset_hardware_watchpoint(u32 tid, u32 wp_id);
};

// ===========================================================================
// AllocationTracker
// ===========================================================================
struct AllocationBlock {
    void* address{nullptr};
    usize size{0};
    u32   flags{0};
};

class AllocationTracker {
public:
    AllocationTracker();
    ~AllocationTracker();
    YAM_NONCOPYABLE(AllocationTracker);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    void begin(u32 flags);
    bool end();
    u32   block_count() const;
    usize block_total_size() const;
    std::vector<AllocationBlock> blocks() const;
private:
    YamAllocationTracker* raw_{nullptr};
};

// ===========================================================================
// InstanceTracker
// ===========================================================================
class InstanceTracker {
public:
    InstanceTracker();
    ~InstanceTracker();
    YAM_NONCOPYABLE(InstanceTracker);
    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    void begin(u32 flags);
    bool end();
    u32  total_count() const;
    std::vector<std::pair<void*, void*>> instances() const;
private:
    YamInstanceTracker* raw_{nullptr};
};

// ===========================================================================
// Arm64Writer — 57 دالة (مطابقة لما ينفذه yam_writer.cpp)
// ===========================================================================
class Arm64Writer {
public:
    explicit Arm64Writer(void* code);
    ~Arm64Writer();
    YAM_NONCOPYABLE(Arm64Writer);
    YAM_NODISCARD bool valid() const noexcept { return w_ != nullptr; }
    YAM_NODISCARD YamArm64Writer* raw() const noexcept { return w_; }

    void* cursor() const;
    u32   offset() const;
    void  reset(void* pc);
    void  flush();
    void  skip(u32 bytes);

    void put_instruction(u32 insn);
    void put_bytes(const ByteVector& d);
    void put_nop();
    void put_ret();
    void put_ret_reg(int reg);
    void put_mov_reg_reg(int dst, int src);
    void put_mov_reg_u64(int dst, u64 v);
    void put_mov_reg_address(int dst, void* addr);
    void put_mov_reg_nzcv(int reg);
    void put_mov_nzcv_reg(int reg);
    void put_add_reg_reg_imm(int dst, int left, u64 right);
    void put_add_reg_reg_reg(int dst, int l, int r);
    void put_sub_reg_reg_imm(int dst, int left, u64 right);
    void put_sub_reg_reg_reg(int dst, int l, int r);
    void put_and_reg_reg_imm(int dst, int left, u64 right);
    void put_eor_reg_reg_reg(int dst, int l, int r);
    void put_cmp_reg_reg(int l, int r);
    void put_tst_reg_imm(int reg, u64 imm);
    void put_ldr_reg_u64(int reg, u64 val);
    void put_ldr_reg_address(int reg, void* addr);
    void put_ldr_reg_u64_ptr(int reg, void* ptr);
    void put_ldr_reg_reg(int dst, int src);
    void put_ldr_reg_reg_offset(int dst, int src, u32 off);
    void put_str_reg_reg(int src, int dst);
    void put_str_reg_reg_offset(int src, int dst, u32 off);
    void put_ldp_reg_reg_reg_offset(int r1, int r2, int r3, u32 off);
    void put_stp_reg_reg_reg_offset(int r1, int r2, int r3, u32 off);
    void put_adrp_reg_address(int reg, void* addr);
    void put_b_imm(u64 imm);
    void put_b_cond_imm(u32 cond, u64 imm);
    void put_bl_imm(u64 imm);
    void put_blr_reg(int reg);
    void put_br_reg(int reg);
    void put_branch_address(void* addr);
    void put_cbz_reg_imm(int reg, u64 imm);
    void put_cbnz_reg_imm(int reg, u64 imm);
    void put_tbz_reg_imm_imm(int reg, u32 bit, u64 imm);
    void put_tbnz_reg_imm_imm(int reg, u32 bit, u64 imm);
    void put_brk_imm(u16 imm);
    void put_bti();
    void put_svc_imm(u16 imm);
    void put_ubfm(int dst, int src, u32 immr, u32 imms);
    void put_lsl_reg_imm(int dst, int src, u32 shift);
    void put_lsr_reg_imm(int dst, int src, u32 shift);
    void put_uxtw_reg_reg(int dst, int src);
    void put_push_reg_reg(int r1, int r2);
    void put_pop_reg_reg(int r1, int r2);
    void put_push_all_x();
    void put_pop_all_x();
private:
    YamArm64Writer* w_{nullptr};
};

// ===========================================================================
// Arm64Relocator
// ===========================================================================
class Arm64Relocator {
public:
    Arm64Relocator(void* input, Arm64Writer* writer);
    ~Arm64Relocator();
    YAM_NONCOPYABLE(Arm64Relocator);
    YAM_NODISCARD bool valid() const noexcept { return r_ != nullptr; }
    u32  read_one();
    void write_all();
    void write_one();
    void skip_one();
    bool eob() const;
    bool eoi() const;
    void set_scratch_reg(int reg);
    int  pick_exit_reg();
    static bool can_relocate(void* addr, u32 min_bytes, u32 max_range, void** out);
private:
    YamArm64Relocator* r_{nullptr};
};

// ===========================================================================
// CodeSegment / CodeAllocator / CodeDeflector / CodeSlice
// ===========================================================================
class CodeSegment {
public:
    struct Options {
        usize  code_size{4096};
        usize  data_size{1024};
        u32    alignment{64};
        usize  page_size{4096};
        void*  near{nullptr};
        bool   writable{true};
    };
    CodeSegment();
    explicit CodeSegment(const Options& opt);
    ~CodeSegment();
    YAM_NONCOPYABLE(CodeSegment);
    YAM_NODISCARD bool valid() const noexcept { return s_ != nullptr; }
    YAM_NODISCARD void* address() const;
    YAM_NODISCARD usize size() const;
    YAM_NODISCARD usize virtual_size() const;
    void realize();
    void map(usize offset, usize size, void* addr);
    static bool mark(void* code, usize size);
private:
    YamCodeSegment* s_{nullptr};
};

class CodeAllocator {
public:
    explicit CodeAllocator(usize slice_size = 64);
    ~CodeAllocator();
    YAM_NONCOPYABLE(CodeAllocator);
    YAM_NODISCARD bool valid() const noexcept { return a_ != nullptr; }
    Result<void*> alloc_slice();
    Result<void*> try_alloc_slice_near(void* near, usize max_distance, usize alignment);
    void commit();
private:
    YamCodeAllocator* a_{nullptr};
};

// ===========================================================================
// ElfModule
// ===========================================================================
struct ElfSectionDetails {
    String name;
    u32    type{0};
    u32    flags{0};
    void*  address{nullptr};
    usize  offset{0};
    usize  size{0};
};

struct ElfSymbolDetails {
    String name;
    void*  address{nullptr};
    u32    type{0};
    u32    bind{0};
    usize  size{0};
};

struct ElfSegmentDetails {
    u32   type{0};
    u32   flags{0};
    void* address{nullptr};
    usize file_offset{0};
    usize file_size{0};
    usize memory_size{0};
};

class ElfModule {
public:
    ElfModule() = default;
    static Result<ElfModule> from_file(const String& path);
    static Result<ElfModule> from_blob(const void* blob, usize size);
    static Result<ElfModule> from_memory(const String& path, void* base);
    ~ElfModule();
    YAM_NONCOPYABLE(ElfModule);
    YAM_DEFAULT_MOVABLE(ElfModule);
    YAM_NODISCARD bool valid() const noexcept { return m_ != nullptr; }
    Result<void> load();
    String source_path() const;
    void*  base_address() const;
    usize  mapped_size() const;
    void*  entrypoint() const;
    u32    pointer_size() const;
    String interpreter() const;
    std::vector<ElfSectionDetails> sections() const;
    std::vector<ElfSymbolDetails>  symbols() const;
    std::vector<ElfSegmentDetails> segments() const;
    std::vector<String>            dependencies() const;
private:
    YamElfModule* m_{nullptr};
};

// ===========================================================================
// MetalArray / MetalHashTable
// ===========================================================================
class MetalArray {
public:
    MetalArray();
    ~MetalArray();
    YAM_NONCOPYABLE(MetalArray);
    YAM_NODISCARD bool valid() const noexcept { return arr_ != nullptr; }
    void init(usize element_size);
    void* at(u32 index);
    void* append();
    void* insert_at(u32 index);
    void remove_at(u32 index);
    void remove_all();
    void ensure_capacity(u32 capacity);
    void extents(void** start, void** end);
private:
    void* arr_{nullptr};
};

class MetalHashTable {
public:
    MetalHashTable();
    ~MetalHashTable();
    YAM_NONCOPYABLE(MetalHashTable);
    YAM_NODISCARD bool valid() const noexcept { return t_ != nullptr; }
    bool insert(void* key, void* value);
    bool replace(void* key, void* value);
    bool remove(const void* key);
    void remove_all();
    void* lookup(const void* key) const;
    bool contains(const void* key) const;
    u32 size() const;
    YamMetalHashTable* raw() const noexcept { return t_; }
private:
    YamMetalHashTable* t_{nullptr};
};

} // namespace yam

#endif
