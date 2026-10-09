#ifndef YAM_STALKER_HPP
#define YAM_STALKER_HPP

// ===========================================================================
// yam_stalker.hpp — Full Stalker wrapper (NO STUBS)
// ===========================================================================

#include "yam.hpp"
#include "yam_c_api.hpp"

namespace yam {

enum class StalkerEvent : u32 {
    None    = 0,
    Call    = 1u << 0,
    Ret     = 1u << 1,
    Exec    = 1u << 2,
    Block   = 1u << 3,
    Compile = 1u << 4,
};

inline StalkerEvent operator|(StalkerEvent a, StalkerEvent b) noexcept {
    return static_cast<StalkerEvent>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline bool has_flag(StalkerEvent e, StalkerEvent f) noexcept {
    return (static_cast<u32>(e) & static_cast<u32>(f)) != 0;
}

// ---------------------------------------------------------------------------
// StalkerIterator
// ---------------------------------------------------------------------------

class StalkerIterator {
public:
    StalkerIterator() = default;
    explicit StalkerIterator(YamStalkerIterator* it, YamStalkerOutput* out)
        : it_(it), out_(out) {}

    YAM_NODISCARD YamStalkerIterator* raw() const noexcept { return it_; }
    YAM_NODISCARD YamStalkerOutput*   output() const noexcept { return out_; }
    YAM_NODISCARD bool valid() const noexcept { return it_ != nullptr; }

    // Keep the current instruction.
    void keep();

    // Advance to the next instruction. Returns the Capstone insn pointer.
    const void* next();

    // Insert a callout before the current instruction.
    void put_callout(YamStalkerCallout cb, void* user_data,
                     GDestroyNotify destroy = nullptr);

    // Return from the current block to the caller (chaining return).
    void put_chaining_return();

    YAM_NODISCARD void* get_memory_access() const;
    YAM_NODISCARD void* get_capstone() const;

private:
    YamStalkerIterator* it_{nullptr};
    YamStalkerOutput*   out_{nullptr};
};

// ---------------------------------------------------------------------------
// StalkerTransformer
// ---------------------------------------------------------------------------

class StalkerTransformer {
public:
    using TransformFn = std::function<void(StalkerIterator&)>;

    StalkerTransformer() = default;
    explicit StalkerTransformer(YamStalkerTransformer* t) : t_(t) {}
    ~StalkerTransformer();

    YAM_NONCOPYABLE(StalkerTransformer);
    YAM_DEFAULT_MOVABLE(StalkerTransformer);

    YAM_NODISCARD YamStalkerTransformer* raw() const noexcept { return t_; }
    YAM_NODISCARD bool valid() const noexcept { return t_ != nullptr; }

    static StalkerTransformer make_default();
    static StalkerTransformer make_from_callback(TransformFn fn);

private:
    YamStalkerTransformer* t_{nullptr};
    std::shared_ptr<TransformFn> fn_;
};

// ---------------------------------------------------------------------------
// Stalker
// ---------------------------------------------------------------------------

class Stalker {
public:
    Stalker();
    ~Stalker();
    YAM_NONCOPYABLE(Stalker);

    YAM_NODISCARD bool valid() const noexcept { return raw_ != nullptr; }
    YAM_NODISCARD YamStalker* raw() const noexcept { return raw_; }

    // Follow the current thread.
    Result<void> follow_me(StalkerTransformer* transformer = nullptr,
                           void* event_sink = nullptr);

    // Stop following the current thread.
    Result<void> unfollow_me();

    // Follow a specific thread.
    Result<void> follow(u64 thread_id,
                        StalkerTransformer* transformer = nullptr,
                        void* event_sink = nullptr);

    // Stop following a specific thread.
    Result<void> unfollow(u64 thread_id);

    // Activate / deactivate the Stalker.
    void activate(bool on);
    void deactivate();

    // Flush pending events.
    void flush();

    // Garbage collect.
    void garbage_collect();

    // Add a call probe. Returns a probe id (>0) or 0 on error.
    u32 add_call_probe(void* target, GCallback cb, void* data);

    // Remove a call probe.
    void remove_call_probe(u32 probe_id);

    // Exclude a range from instrumentation.
    Result<void> exclude(void* start, usize size);

    // Invalidate cached code at an address.
    void invalidate(void* code_address);
    void invalidate_for_thread(u64 thread_id);

    // Trust threshold.
    void set_trust_threshold(int t);
    YAM_NODISCARD int get_trust_threshold();

    // Stop everything.
    void stop();

    // Static helpers.
    static bool is_supported();
    bool is_following_me() const;

    // Prefetch / recompile.
    void prefetch(void* code_address);
    void recompile(void* code_address);

    // Run a callback on a thread.
    void run_on_thread(u64 thread_id, GCallback cb, void* data);
    void run_on_thread_sync(u64 thread_id, GCallback cb, void* data);

private:
    YamStalker* raw_{nullptr};
};

inline Stalker& stalker() {
    static Stalker inst;
    return inst;
}

} // namespace yam

#endif // YAM_STALKER_HPP
