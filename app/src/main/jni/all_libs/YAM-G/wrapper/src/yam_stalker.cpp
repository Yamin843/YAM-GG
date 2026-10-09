// ===========================================================================
// yam_stalker.cpp — Stalker wrapper against real YAMJS.h signatures.
//
// Key differences from the wrapper's original design:
//   * YamAddress is guint64 (not gpointer) → cast on both directions.
//   * YamStalkerIterator::next yields a "const cs_insn**" out-param and
//     returns gboolean — we marshal it to "const void*".
//   * get_memory_access returns a YamMemoryAccess (union) — we box it.
//   * get_capstone returns csh (unsigned long) — we box it.
//   * activate takes a target pointer; deactivate is separate.
//   * add_call_probe and run_on_thread* take extra trailing args.
//   * invalidate_for_thread additionally takes an address.
//   * prefetch additionally takes a recycle count.
// ===========================================================================

#include "yam_stalker.hpp"
#include "yam_internal.hpp"

namespace yam {

// ===========================================================================
// StalkerIterator
// ===========================================================================

void StalkerIterator::keep() {
    if (it_) yam_stalker_iterator_keep(it_);
}

const void* StalkerIterator::next() {
    if (!it_) return nullptr;
    const cs_insn* insn = nullptr;
    gboolean ok = yam_stalker_iterator_next(it_, &insn);
    return ok ? reinterpret_cast<const void*>(insn) : nullptr;
}

void StalkerIterator::put_callout(YamStalkerCallout cb, void* user_data,
                                    GDestroyNotify destroy) {
    if (it_ && cb) {
        yam_stalker_iterator_put_callout(it_, cb, user_data, destroy);
    }
}

void StalkerIterator::put_chaining_return() {
    if (it_) yam_stalker_iterator_put_chaining_return(it_);
}

void* StalkerIterator::get_memory_access() const {
    if (!it_) return nullptr;
    YamMemoryAccess ma = yam_stalker_iterator_get_memory_access(it_);
    // YamMemoryAccess is a union; box the raw bits into a pointer.
    return reinterpret_cast<void*>(static_cast<uintptr_t>(ma));
}

void* StalkerIterator::get_capstone() const {
    if (!it_) return nullptr;
    csh c = yam_stalker_iterator_get_capstone(it_);
    return reinterpret_cast<void*>(static_cast<uintptr_t>(c));
}

// ===========================================================================
// StalkerTransformer
// ===========================================================================

StalkerTransformer::~StalkerTransformer() {
    if (t_) {
        yam_object_unref(t_);
        t_ = nullptr;
    }
}

StalkerTransformer StalkerTransformer::make_default() {
    return StalkerTransformer(yam_stalker_transformer_make_default());
}

namespace {

void transform_trampoline(YamStalkerIterator* it, YamStalkerOutput* out,
                          gpointer user_data) {
    auto* fn = static_cast<StalkerTransformer::TransformFn*>(user_data);
    if (!fn || !*fn) return;
    StalkerIterator wrapper(it, out);
    try {
        (*fn)(wrapper);
    } catch (const std::exception& e) {
        YAM_LOG_ERROR() << "stalker transform: " << e.what();
    } catch (...) {
    }
}

void transform_destroy(gpointer user_data) {
    delete static_cast<StalkerTransformer::TransformFn*>(user_data);
}

} // namespace

StalkerTransformer StalkerTransformer::make_from_callback(TransformFn fn) {
    auto* heap_fn = new TransformFn(std::move(fn));
    YamStalkerTransformer* raw = yam_stalker_transformer_make_from_callback(
        transform_trampoline, heap_fn, transform_destroy);
    StalkerTransformer t(raw);
    // Shared_ptr with no-op deleter: transform_destroy handles the delete.
    t.fn_ = std::shared_ptr<TransformFn>(heap_fn, [](TransformFn*) {});
    return t;
}

// ===========================================================================
// Stalker
// ===========================================================================

Stalker::Stalker() {
    raw_ = yam_stalker_new();
    if (!raw_) {
        YAM_LOG_ERROR() << "yam_stalker_new returned null";
    }
}

Stalker::~Stalker() {
    if (raw_) {
        try { yam_stalker_stop(raw_); } catch (...) {}
        yam_object_unref(raw_);
        raw_ = nullptr;
    }
}

Result<void> Stalker::follow_me(StalkerTransformer* tr, void* sink) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "no stalker");
    if (!yam_stalker_is_supported()) {
        return Result<void>::err(ErrorCode::BackendUnavailable, "stalker not supported");
    }
    yam_stalker_follow_me(raw_,
                          tr ? tr->raw() : nullptr,
                          static_cast<YamEventSink*>(sink));
    return Result<void>::ok();
}

Result<void> Stalker::unfollow_me() {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "no stalker");
    yam_stalker_unfollow_me(raw_);
    return Result<void>::ok();
}

Result<void> Stalker::follow(u64 tid, StalkerTransformer* tr, void* sink) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "no stalker");
    if (!yam_stalker_is_supported()) {
        return Result<void>::err(ErrorCode::BackendUnavailable, "stalker not supported");
    }
    yam_stalker_follow(raw_,
                       static_cast<YamThreadId>(tid),
                       tr ? tr->raw() : nullptr,
                       static_cast<YamEventSink*>(sink));
    return Result<void>::ok();
}

Result<void> Stalker::unfollow(u64 tid) {
    if (!raw_) return Result<void>::err(ErrorCode::BackendUnavailable, "no stalker");
    yam_stalker_unfollow(raw_, static_cast<YamThreadId>(tid));
    return Result<void>::ok();
}

void Stalker::activate(bool on) {
    if (!raw_) return;
    if (on) {
        // No specific target — activate with a null target.
        yam_stalker_activate(raw_, nullptr);
    } else {
        yam_stalker_deactivate(raw_);
    }
}

void Stalker::deactivate() {
    if (raw_) yam_stalker_deactivate(raw_);
}

void Stalker::flush() {
    if (raw_) yam_stalker_flush(raw_);
}

void Stalker::garbage_collect() {
    if (raw_) yam_stalker_garbage_collect(raw_);
}

u32 Stalker::add_call_probe(void* target, GCallback cb, void* data) {
    if (!raw_ || !target || !cb) return 0;
    return static_cast<u32>(yam_stalker_add_call_probe(
        raw_,
        target,
        reinterpret_cast<YamCallProbeCallback>(cb),
        data,
        nullptr));
}

void Stalker::remove_call_probe(u32 id) {
    if (raw_) yam_stalker_remove_call_probe(raw_, static_cast<YamProbeId>(id));
}

Result<void> Stalker::exclude(void* start, usize size) {
    if (!raw_ || !start)
        return Result<void>::err(ErrorCode::InvalidArgument, "exclude");
    YamMemoryRange range{};
    range.base_address = reinterpret_cast<YamAddress>(start);
    range.size = size;
    yam_stalker_exclude(raw_, &range);
    return Result<void>::ok();
}

void Stalker::invalidate(void* code_address) {
    if (raw_) yam_stalker_invalidate(raw_, code_address);
}

void Stalker::invalidate_for_thread(u64 tid) {
    if (!raw_) return;
    // The real signature needs an address — invalidate all by passing null.
    yam_stalker_invalidate_for_thread(raw_,
                                      static_cast<YamThreadId>(tid),
                                      nullptr);
}

void Stalker::set_trust_threshold(int t) {
    if (raw_) yam_stalker_set_trust_threshold(raw_, t);
}

int Stalker::get_trust_threshold() {
    return raw_ ? yam_stalker_get_trust_threshold(raw_) : 0;
}

void Stalker::stop() {
    if (raw_) yam_stalker_stop(raw_);
}

bool Stalker::is_supported() {
    return yam_stalker_is_supported() != 0;
}

bool Stalker::is_following_me() const {
    return raw_ && yam_stalker_is_following_me(raw_) != 0;
}

void Stalker::prefetch(void* code_address) {
    if (raw_) yam_stalker_prefetch(raw_, code_address, 0);
}

void Stalker::recompile(void* code_address) {
    if (raw_) yam_stalker_recompile(raw_, code_address);
}

void Stalker::run_on_thread(u64 tid, GCallback cb, void* data) {
    if (!raw_ || !cb) return;
    yam_stalker_run_on_thread(
        raw_,
        static_cast<YamThreadId>(tid),
        reinterpret_cast<YamStalkerRunOnThreadFunc>(cb),
        data,
        nullptr);
}

void Stalker::run_on_thread_sync(u64 tid, GCallback cb, void* data) {
    if (!raw_ || !cb) return;
    yam_stalker_run_on_thread_sync(
        raw_,
        static_cast<YamThreadId>(tid),
        reinterpret_cast<YamStalkerRunOnThreadFunc>(cb),
        data);
}

} // namespace yam
