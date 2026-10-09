#ifndef YAM_INTERNAL_HPP
#define YAM_INTERNAL_HPP

// ===========================================================================
// yam_internal.hpp — private declarations
// ===========================================================================

#include "yam.hpp"
#include "yam_c_api.hpp"
#include "yam_advanced.hpp"
#include "yam_subsystems.hpp"
#include "yam_cmodule_registry.hpp"

#include "YAMJS.h"
#include "gumpp.hpp"
#include "invocationcontext.hpp"
#include "invocationlistener.hpp"
#include "objectwrapper.hpp"
#include "podwrapper.hpp"
#include "runtime.hpp"
#include "string.hpp"

#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <cxxabi.h>

namespace yam { namespace detail {

// ===========================================================================
// SECTION A — ListenerRegistry
// ===========================================================================

class ListenerRegistry {
public:
    struct Entry {
        InvocationHook on_enter;
        InvocationHook on_leave;
        InvocationHook on_hit;
        std::atomic<bool> alive{true};
        void* user_data{nullptr};
    };
    static ListenerRegistry& instance();
    std::shared_ptr<Entry> make_entry(InvocationHook e, InvocationHook l, InvocationHook h);
    void drop(const std::shared_ptr<Entry>& e);
    std::shared_ptr<Entry> get(void* key) const;
    void put(void* key, std::shared_ptr<Entry> e);
    void erase(void* key);
    usize size() const;
private:
    ListenerRegistry() = default;
    mutable std::mutex mu_;
    std::unordered_map<void*, std::shared_ptr<Entry>> map_;
};

// ===========================================================================
// SECTION B — (JavaSync moved to yam.hpp)
// ===========================================================================

// ===========================================================================
// SECTION C — Handle pool
// ===========================================================================

class HandlePool {
public:
    using Id = u64;
    static constexpr Id kInvalid = 0;
    Id allocate() noexcept;
    void reset() noexcept;
    Id peek_next() const noexcept;
private:
    std::atomic<Id> next_{1};
};

// ===========================================================================
// SECTION D — LRU cache
// ===========================================================================

template <typename K, typename V, typename Hash = std::hash<K>>
class LruCache {
public:
    using DestroyFn = std::function<void(V&)>;
    LruCache(usize cap, DestroyFn d = nullptr)
        : cap_(cap ? cap : 1), destroy_(std::move(d)) {}
    usize size() const noexcept { return map_.size(); }
    bool contains(const K& k) const { return map_.find(k) != map_.end(); }
    V* get(const K& k) {
        auto it = map_.find(k);
        if (it == map_.end()) return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second->it);
        return &it->second->value;
    }
    void put(const K& k, const V& v) {
        auto it = map_.find(k);
        if (it != map_.end()) {
            if (destroy_) destroy_(it->second->value);
            it->second->value = v;
            lru_.splice(lru_.begin(), lru_, it->second->it);
            return;
        }
        lru_.push_front(k);
        auto h = std::make_shared<Holder>();
        h->value = v;
        h->it = lru_.begin();
        map_[k] = h;
        while (map_.size() > cap_) evict_one();
    }
    void erase(const K& k) {
        auto it = map_.find(k);
        if (it == map_.end()) return;
        if (destroy_) destroy_(it->second->value);
        lru_.erase(it->second->it);
        map_.erase(it);
    }
    void clear() {
        if (destroy_) for (auto& p : map_) destroy_(p.second->value);
        map_.clear();
        lru_.clear();
    }
private:
    struct Holder { V value; typename std::list<K>::iterator it; };
    void evict_one() {
        if (lru_.empty()) return;
        auto k = lru_.back();
        auto it = map_.find(k);
        if (it != map_.end()) {
            if (destroy_) destroy_(it->second->value);
            map_.erase(it);
        }
        lru_.pop_back();
    }
    usize cap_;
    DestroyFn destroy_;
    std::list<K> lru_;
    std::unordered_map<K, std::shared_ptr<Holder>, Hash> map_;
};

// ===========================================================================
// SECTION E — JSON parser state (used by JsonValue::parse)
// ===========================================================================

class JsonParserImpl {
public:
    explicit JsonParserImpl(StringView s) : s_(s) {}
    Result<JsonValue> parse();
private:
    StringView s_;
    usize pos_{0};

    void skip_ws();
    Result<JsonValue> parse_value();
    Result<JsonValue> parse_string();
    Result<JsonValue> parse_number();
    Result<JsonValue> parse_array();
    Result<JsonValue> parse_object();
    Result<JsonValue> parse_true();
    Result<JsonValue> parse_false();
    Result<JsonValue> parse_null();
    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    bool consume(char c);
};

// ===========================================================================
// SECTION F — WeakSymbols
// ===========================================================================

struct WeakSymbols {
    void* (*find_function_ptr)(const char*){nullptr};
    Yam::PtrArray* (*find_matching_functions_array)(const char*){nullptr};
    bool loaded{false};
};
WeakSymbols& weak_symbols();
void load_weak_symbols();

// ===========================================================================
// SECTION G — JSON helpers
// ===========================================================================

namespace json {
    String get_string(const String& s, const char* key, const String& def = "");
    i64 get_i64(const String& s, const char* key, i64 def = 0);
    bool get_bool(const String& s, const char* key, bool def = false);
    std::vector<String> strings_of_array(const String& s);
    std::vector<String> object_bodies_of_array(const String& s, const char* key);
}

// ===========================================================================
// SECTION H — Trace helpers
// ===========================================================================

void trace_enable(bool on);
bool trace_enabled() noexcept;
void trace(const char* fmt, ...);

// ===========================================================================
// SECTION I — Message handler dispatch
// ===========================================================================

// Called from the JS bridge when a message arrives.
void handle_js_message(const String& raw_message);

// Post to a channel that JS recv() is listening on.
void post_to_channel_typed(const String& channel, const JsonValue& payload);
void post_to_channel_json(const String& channel, const String& payload_json);

// ===========================================================================
// SECTION J — Memory allocation registry
// ===========================================================================

void register_allocation(void* addr, usize size);
bool unmap_allocation(void* addr);

// ===========================================================================
// SECTION K — Bootstrap source
// ===========================================================================

extern const char* const kBootstrapSrc;

// ===========================================================================
// SECTION L — Value <-> JsonValue conversion
// ===========================================================================

JsonValue value_to_json(const Value& v);
Value json_to_value(const JsonValue& j);

// ===========================================================================
// SECTION M — /proc/self/maps parsing
// ===========================================================================

struct ProcMapsEntry {
    u64        start{0};
    u64        end{0};
    Protection prot{Protection::None};
    String     path;
};

class ProcMaps {
public:
    static std::vector<ProcMapsEntry> read();
};

// ===========================================================================
// SECTION N — Hex pattern compiler
// ===========================================================================
//
// Pattern grammar:
//   "AB CD EF"          exact bytes
//   "AB ?? CD"          0xFF wildcard
//   "A? ?F"             nibble wildcards (both nibbles optional)
// Whitespace and 0x prefix are ignored.
// Result vector: 0xFF means "any byte", else exact byte.
// ===========================================================================

class HexPattern {
public:
    static Result<ByteVector> compile(const String& pattern);
};

}} // namespace yam::detail

#endif // YAM_INTERNAL_HPP
