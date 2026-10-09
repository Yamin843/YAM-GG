// ===========================================================================
// yam_events.cpp — Full event router + all event types
// ===========================================================================

#include "yam_java_model.hpp"
#include "yam_internal.hpp"

namespace yam {

// ===========================================================================
// SECTION 1 — events:: dispatcher
// ===========================================================================

namespace events {

namespace {
struct Dispatcher {
    std::mutex mu;
    std::unordered_map<String, std::vector<std::function<void(const Event&)>>> handlers;
    std::vector<std::function<void(const Event&)>> globals;
    std::atomic<u64> dispatched{0};
};
Dispatcher& inst() { static Dispatcher d; return d; }
}

void on(const String& type, std::function<void(const Event&)> h) {
    auto& d = inst();
    std::lock_guard<std::mutex> lk(d.mu);
    d.handlers[type].push_back(std::move(h));
}
void on_any(std::function<void(const Event&)> h) {
    auto& d = inst();
    std::lock_guard<std::mutex> lk(d.mu);
    d.globals.push_back(std::move(h));
}
void off(const String& type) {
    auto& d = inst();
    std::lock_guard<std::mutex> lk(d.mu);
    d.handlers.erase(type);
}
void clear() {
    auto& d = inst();
    std::lock_guard<std::mutex> lk(d.mu);
    d.handlers.clear();
    d.globals.clear();
}
std::vector<String> registered_types() {
    auto& d = inst();
    std::lock_guard<std::mutex> lk(d.mu);
    std::vector<String> out;
    out.reserve(d.handlers.size());
    for (auto& p : d.handlers) out.push_back(p.first);
    return out;
}
u64 count() { return inst().dispatched.load(std::memory_order_relaxed); }

void dispatch(const Event& ev) {
    auto& d = inst();
    d.dispatched.fetch_add(1, std::memory_order_relaxed);

    std::vector<std::function<void(const Event&)>> local;
    std::vector<std::function<void(const Event&)>> gl;
    {
        std::lock_guard<std::mutex> lk(d.mu);
        auto it = d.handlers.find(ev.type);
        if (it != d.handlers.end()) local = it->second;
        gl = d.globals;
    }
    for (auto& h : local) {
        try { h(ev); }
        catch (const std::exception& e) {
            YAM_LOG_ERROR() << "event " << ev.type << ": " << e.what();
        }
    }
    for (auto& h : gl) {
        try { h(ev); } catch (...) {}
    }
}

} // namespace events

// ===========================================================================
// SECTION 2 — trace
// ===========================================================================

namespace trace {

namespace {
std::mutex g_mu;
std::unordered_map<u64, Target> g_targets;
std::atomic<u64> g_next_id{1};
}

Result<u64> add(const String& cls, const String& m, const String& sig,
                i32 stack, i32 skip, i32 min_ms) {
    u64 id = g_next_id.fetch_add(1);
    auto r = JavaScriptBridge::instance().trace(id, cls, m, sig, stack, skip,
                                                 min_ms, false, "");
    if (!r) return Result<u64>::err(r.error_code(), r.error_message());
    Target t;
    t.trace_id = id;
    t.class_name = cls;
    t.method_name = m;
    t.signature = sig;
    std::lock_guard<std::mutex> lk(g_mu);
    g_targets[id] = t;
    return Result<u64>::ok(id);
}

Result<u64> add_class(const String& cls, const String& pattern,
                       i32 stack, i32 skip, i32 min_ms) {
    u64 id = g_next_id.fetch_add(1);
    auto r = JavaScriptBridge::instance().trace_class(id, cls, pattern, stack,
                                                       skip, min_ms);
    if (!r) return Result<u64>::err(r.error_code(), r.error_message());
    Target t;
    t.trace_id = id;
    t.class_name = cls;
    t.method_name = "*" + pattern;
    std::lock_guard<std::mutex> lk(g_mu);
    g_targets[id] = t;
    return Result<u64>::ok(id);
}

Result<void> remove(u64 id) {
    auto r = JavaScriptBridge::instance().untrace(id);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    std::lock_guard<std::mutex> lk(g_mu);
    g_targets.erase(id);
    return Result<void>::ok();
}
Result<void> remove_range(u64 first, u64 last) {
    auto r = JavaScriptBridge::instance().untrace_range(first, last);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    std::lock_guard<std::mutex> lk(g_mu);
    for (u64 i = first; i <= last; ++i) g_targets.erase(i);
    return Result<void>::ok();
}
Result<void> clear() {
    auto r = JavaScriptBridge::instance().clear_traces();
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    std::lock_guard<std::mutex> lk(g_mu);
    g_targets.clear();
    return Result<void>::ok();
}
Result<void> pause(u64 id) {
    auto r = JavaScriptBridge::instance().pause(id);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_targets.find(id);
    if (it != g_targets.end()) it->second.paused = true;
    return Result<void>::ok();
}
Result<void> resume(u64 id) {
    auto r = JavaScriptBridge::instance().resume(id);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_targets.find(id);
    if (it != g_targets.end()) it->second.paused = false;
    return Result<void>::ok();
}
std::vector<Target> active() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Target> out;
    for (auto& p : g_targets) out.push_back(p.second);
    return out;
}

} // namespace trace

// ===========================================================================
// SECTION 3 — bp (breakpoints)
// ===========================================================================

namespace bp {

namespace {
std::mutex g_mu;
std::function<void(const Hit&)> g_handler;

void handle_bp_hit(const Event& ev) {
    Hit h;
    h.bp_id       = ev.get_u64("bpId", 0);
    h.trace_id    = ev.get_u64("traceId", 0);
    h.class_name  = ev.get_str("className");
    h.method_name = ev.get_str("methodName");
    h.call_index  = ev.get_i64("callIndex", 0);
    h.thread_id   = ev.get_u64("threadId", 0);
    h.timestamp   = ev.get_i64("timestamp", 0);
    if (auto* v = ev.get("args")) h.args = *v;
    if (auto* v = ev.get("thisObj")) h.this_obj = *v;
    if (auto* v = ev.get("stack")) h.stack = *v;

    std::function<void(const Hit&)> cb;
    { std::lock_guard<std::mutex> lk(g_mu); cb = g_handler; }

    YAM_LOG_INFO() << "bp hit #" << h.bp_id << " " << h.class_name
                   << "::" << h.method_name;

    if (cb) {
        try { cb(h); }
        catch (const std::exception& e) { YAM_LOG_ERROR() << "bp cb: " << e.what(); }
    } else {
        continue_bp(h.bp_id);
    }
}

struct AutoReg {
    AutoReg() {
        events::on("bp_hit", handle_bp_hit);
        events::on("bp_ack", [](const Event& ev) {
            YAM_LOG_INFO() << "bp_ack #" << ev.get_u64("traceId", 0)
                           << " enabled=" << ev.get_bool("enabled", false);
        });
    }
};
AutoReg g_auto;
} // namespace

void set_handler(std::function<void(const Hit&)> h) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_handler = std::move(h);
}

Result<void> set(u64 tid, bool enabled, i32 every) {
    auto r = JavaScriptBridge::instance().set_breakpoint(tid, enabled, every);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

Result<void> continue_bp(u64 bp_id) {
    JsonValue payload;
    payload.type = JsonValue::Type::Object;
    payload.obj_val["action"] = JsonValue("continue");
    String channel = "bp_reply_" + std::to_string(bp_id);
    JavaScriptBridge::instance().post_raw(channel, payload);
    return Result<void>::ok();
}

Result<void> throw_bp(u64 bp_id, const String& msg) {
    JsonValue payload;
    payload.type = JsonValue::Type::Object;
    payload.obj_val["action"] = JsonValue("throw");
    payload.obj_val["throwMessage"] = JsonValue(msg);
    String channel = "bp_reply_" + std::to_string(bp_id);
    JavaScriptBridge::instance().post_raw(channel, payload);
    return Result<void>::ok();
}

Result<void> continue_with_args(u64 bp_id, const std::vector<JsonValue>& args) {
    JsonValue payload;
    payload.type = JsonValue::Type::Object;
    payload.obj_val["action"] = JsonValue("continue");
    JsonValue arr;
    arr.type = JsonValue::Type::Array;
    for (auto& a : args) arr.arr_val.push_back(a);
    payload.obj_val["newArgs"] = std::move(arr);
    String channel = "bp_reply_" + std::to_string(bp_id);
    JavaScriptBridge::instance().post_raw(channel, payload);
    return Result<void>::ok();
}

Result<void> modify_this(u64 bp_id, const std::vector<JsonValue>& fields) {
    JsonValue payload;
    payload.type = JsonValue::Type::Object;
    payload.obj_val["action"] = JsonValue("continue");
    JsonValue arr;
    arr.type = JsonValue::Type::Array;
    for (auto& f : fields) arr.arr_val.push_back(f);
    payload.obj_val["thisFields"] = std::move(arr);
    String channel = "bp_reply_" + std::to_string(bp_id);
    JavaScriptBridge::instance().post_raw(channel, payload);
    return Result<void>::ok();
}

void cancel_all() {
    JavaScriptBridge::instance().cancel_all_breakpoints();
}

void set_timeout(i64 ms) {
    JavaScriptBridge::instance().set_bp_timeout(ms);
}

} // namespace bp

// ===========================================================================
// SECTION 4 — params
// ===========================================================================

namespace params {

Result<void> set(u64 tid, i32 index, const String& type, const String& value) {
    auto r = JavaScriptBridge::instance().set_param(tid, index, type, value);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> clear(u64 tid, i32 index) {
    auto r = JavaScriptBridge::instance().clear_param(tid, index);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> set_spec(u64 tid, i32 index, const JsonValue& spec) {
    auto r = JavaScriptBridge::instance().set_param_spec(tid, index,
                                                          spec.stringify());
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

} // namespace params

// ===========================================================================
// SECTION 5 — chunks
// ===========================================================================

namespace chunks {

namespace {
std::mutex g_mu;
std::unordered_map<u64, Session> g_sessions;
std::function<void(const String&, const String&)> g_full;

void handle_begin(const Event& ev) {
    Session s;
    s.id = ev.get_u64("sessionId", 0);
    s.kind = ev.get_str("kind");
    s.total = static_cast<usize>(ev.get_i64("totalChunks", 0));
    s.started_ms = time_util::now_ms();
    std::lock_guard<std::mutex> lk(g_mu);
    g_sessions[s.id] = s;
    YAM_LOG_DEBUG() << "chunk begin #" << s.id << " kind=" << s.kind
                    << " total=" << s.total;
}

void handle_data(const Event& ev) {
    u64 id = ev.get_u64("sessionId", 0);
    String data = ev.get_str("data");
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_sessions.find(id);
    if (it == g_sessions.end()) return;
    it->second.buffer += data;
    it->second.received++;
}

void handle_end(const Event& ev) {
    u64 id = ev.get_u64("sessionId", 0);
    Session s;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_sessions.find(id);
        if (it == g_sessions.end()) return;
        s = it->second;
        g_sessions.erase(it);
    }
    std::function<void(const String&, const String&)> cb;
    { std::lock_guard<std::mutex> lk(g_mu); cb = g_full; }
    if (cb) {
        try { cb(s.kind, s.buffer); }
        catch (const std::exception& e) { YAM_LOG_ERROR() << "chunk cb: " << e.what(); }
    } else {
        YAM_LOG_DEBUG() << "chunk complete #" << id << " kind=" << s.kind
                        << " bytes=" << s.buffer.size();
    }
}

struct AutoReg {
    AutoReg() {
        events::on("chunk_begin", handle_begin);
        events::on("chunk_data",  handle_data);
        events::on("chunk_end",   handle_end);
    }
};
AutoReg g_auto;
}

void set_full_handler(std::function<void(const String&, const String&)> h) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_full = std::move(h);
}

std::vector<Session> active() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Session> out;
    for (auto& p : g_sessions) out.push_back(p.second);
    return out;
}

} // namespace chunks

// ===========================================================================
// SECTION 6 — watch
// ===========================================================================

namespace watch {

void on_change(std::function<void(const Change&)> cb) {
    events::on("watch_change", [cb](const Event& ev) {
        Change c;
        c.watch_id     = ev.get_u64("id", 0);
        c.handle_id    = ev.get_u64("handleId", 0);
        c.field_name   = ev.get_str("fieldName");
        c.change_count = ev.get_u64("changeCount", 0);
        if (auto* v = ev.get("snapshot")) c.snapshot = *v;
        if (cb) cb(c);
    });
    events::on("watch_start", [](const Event& ev) {
        YAM_LOG_INFO() << "watch started #" << ev.get_u64("id", 0)
                       << " field=" << ev.get_str("fieldName");
    });
    events::on("watch_stop", [](const Event& ev) {
        YAM_LOG_INFO() << "watch stopped #" << ev.get_u64("id", 0);
    });
}

Result<void> start(u64 hid, const String& f, i64 ms) {
    auto r = JavaScriptBridge::instance().watch_start(hid, f, ms);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> stop(u64 wid) {
    auto r = JavaScriptBridge::instance().watch_stop(wid);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> stop_all() {
    auto r = JavaScriptBridge::instance().watch_stop_all();
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

} // namespace watch

// ===========================================================================
// SECTION 7 — user_scripts
// ===========================================================================

namespace user_scripts {

namespace {
std::mutex g_mu;
std::vector<Loaded> g_list;

struct AutoReg {
    AutoReg() {
        events::on("user_script_loaded", [](const Event& ev) {
            Loaded l;
            l.name = ev.get_str("name");
            l.size = static_cast<usize>(ev.get_i64("size", 0));
            l.ok = ev.get_bool("ok", false);
            l.error = ev.get_str("error");
            l.duration_ms = ev.get_i64("durationMs", 0);
            l.loaded_at = time_util::now_ms();
            std::lock_guard<std::mutex> lk(g_mu);
            g_list.push_back(l);
        });
        events::on("scripts_batch_done", [](const Event& ev) {
            YAM_LOG_INFO() << "scripts batch: ok=" << ev.get_i64("ok", 0)
                           << " fail=" << ev.get_i64("fail", 0);
        });
        events::on("script_failed", [](const Event& ev) {
            YAM_LOG_ERROR() << "script failed: " << ev.get_str("name")
                            << " - " << ev.get_str("error");
        });
    }
};
AutoReg g_auto;
}

Result<void> run(const String& name, const String& code) {
    auto r = JavaScriptBridge::instance().run_user_script(name, code);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> run_batch(const std::vector<std::pair<String,String>>& scripts) {
    auto r = JavaScriptBridge::instance().load_scripts_batch(scripts);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
std::vector<Loaded> list() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_list;
}
void clear() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_list.clear();
}

} // namespace user_scripts

// ===========================================================================
// SECTION 8 — replay
// ===========================================================================

namespace replay {

namespace {
std::mutex g_mu;
std::function<void(const ReplayResult&)> g_cb;

void handle_result(const Event& ev) {
    ReplayResult r;
    r.replay_id = ev.get_u64("replayId", 0);
    r.ok = ev.get_bool("ok", false);
    r.error = ev.get_str("error");
    r.total_ms = ev.get_i64("totalMs", 0);
    if (auto* v = ev.get("results")) {
        if (v->is_arr()) {
            for (auto& item : v->arr_val) {
                ResultItem ri;
                if (auto* i = item.get("index")) ri.index = i->as_i64(0);
                if (auto* o = item.get("ok")) ri.ok = o->as_bool(false);
                if (auto* d = item.get("durationMs")) ri.duration_ms = d->as_i64(0);
                if (auto* e = item.get("error")) ri.error = e->as_str();
                r.results.push_back(ri);
            }
        }
    }
    std::function<void(const ReplayResult&)> cb;
    { std::lock_guard<std::mutex> lk(g_mu); cb = g_cb; }
    if (cb) {
        try { cb(r); }
        catch (const std::exception& e) { YAM_LOG_ERROR() << "replay cb: " << e.what(); }
    }
}

struct AutoReg {
    AutoReg() { events::on("replay_result", handle_result); }
};
AutoReg g_auto;
}

void on_result(std::function<void(const ReplayResult&)> cb) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cb = std::move(cb);
}

Result<void> run(u64 rid, const String& cls, const String& m, const String& sig,
                 bool is_static, u64 inst_h,
                 const std::vector<JsonValue>& args, i32 times,
                 i32 interleave_ms, bool stop_on_error) {
    JsonValue arr;
    arr.type = JsonValue::Type::Array;
    for (auto& a : args) arr.arr_val.push_back(a);
    auto r = JavaScriptBridge::instance().replay(
        rid, cls, m, sig, is_static, inst_h, arr.stringify(),
        times, interleave_ms, stop_on_error);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

} // namespace replay

// ===========================================================================
// SECTION 9 — install_event_router (real)
// ===========================================================================




// ===========================================================================
// install_event_router — registers all common event handlers
// ===========================================================================

void install_event_router() {
    static std::once_flag once;
    std::call_once(once, []() {
        // Log level events
        events::on("log", [](const Event& ev) {
            String lvl = ev.get_str("level", "info");
            String msg = ev.get_str("message");
            if (lvl == "error") YAM_LOG_ERROR() << "[js] " << msg;
            else if (lvl == "warn") YAM_LOG_WARN() << "[js] " << msg;
            else YAM_LOG_INFO() << "[js] " << msg;
        });
        events::on("error", [](const Event& ev) {
            YAM_LOG_ERROR() << "[js] " << ev.get_str("message");
        });

        // Trace lifecycle
        events::on("trace_enter", [](const Event& ev) {
            YAM_LOG_DEBUG() << "trace_enter #" << ev.get_u64("traceId", 0)
                            << " " << ev.get_str("className") << "::"
                            << ev.get_str("methodName");
        });
        events::on("trace_leave", [](const Event& ev) {
            YAM_LOG_DEBUG() << "trace_leave #" << ev.get_u64("traceId", 0)
                            << " dur=" << ev.get_i64("durationMs", 0) << "ms";
        });
        events::on("trace_added", [](const Event& ev) {
            YAM_LOG_INFO() << "trace_added #" << ev.get_u64("traceId", 0)
                           << " ok=" << ev.get_bool("ok", false);
        });
        events::on("class_traced", [](const Event& ev) {
            YAM_LOG_INFO() << "class_traced " << ev.get_str("className")
                           << " count=" << ev.get_i64("count", 0);
        });

        // Call / construct results
        events::on("call_result", [](const Event& ev) {
            YAM_LOG_DEBUG() << "call_result ok="
                            << ev.get_bool("ok", false);
        });
        events::on("construct_result", [](const Event& ev) {
            YAM_LOG_DEBUG() << "construct_result ok="
                            << ev.get_bool("ok", false);
        });

        // Class enumeration
        events::on("classes_list", [](const Event& ev) {
            YAM_LOG_INFO() << "classes_list count=" << ev.get_i64("count", 0);
        });
        events::on("loaders_list", [](const Event& ev) {
            YAM_LOG_INFO() << "loaders_list count=" << ev.get_i64("count", 0);
        });
        events::on("class_probe", [](const Event& ev) {
            YAM_LOG_INFO() << "class_probe " << ev.get_str("className");
        });

        // Handle inspection
        events::on("handle_inspect", [](const Event& ev) {
            YAM_LOG_DEBUG() << "handle_inspect id=" << ev.get_u64("handleId", 0);
        });
        events::on("handle_list", [](const Event& ev) {
            YAM_LOG_DEBUG() << "handle_list";
            (void)ev;
        });

        // Field access
        events::on("field_read", [](const Event& ev) {
            YAM_LOG_DEBUG() << "field_read " << ev.get_str("fieldName");
        });
        events::on("field_write", [](const Event& ev) {
            YAM_LOG_DEBUG() << "field_write " << ev.get_str("fieldName");
        });
        events::on("static_read", [](const Event& ev) {
            YAM_LOG_DEBUG() << "static_read " << ev.get_str("className")
                            << "." << ev.get_str("fieldName");
        });
        events::on("static_write", [](const Event& ev) {
            YAM_LOG_DEBUG() << "static_write " << ev.get_str("className")
                            << "." << ev.get_str("fieldName");
        });

        // Paths
        events::on("path_read", [](const Event& ev) {
            YAM_LOG_DEBUG() << "path_read " << ev.get_str("path");
        });
        events::on("path_write", [](const Event& ev) {
            YAM_LOG_DEBUG() << "path_write " << ev.get_str("path");
        });
        events::on("path_invoke", [](const Event& ev) {
            YAM_LOG_DEBUG() << "path_invoke " << ev.get_str("methodName");
        });

        // Template
        events::on("template", [](const Event& ev) {
            YAM_LOG_DEBUG() << "template " << ev.get_str("className") << "::"
                            << ev.get_str("methodName");
        });

        // Param ack
        events::on("param_ack", [](const Event& ev) {
            YAM_LOG_DEBUG() << "param_ack #" << ev.get_u64("traceId", 0)
                            << "[" << ev.get_i64("index", 0) << "]";
        });

        // BP
        events::on("bp_ack", [](const Event& ev) {
            YAM_LOG_INFO() << "bp_ack #" << ev.get_u64("traceId", 0)
                           << " enabled=" << ev.get_bool("enabled", false);
        });

        // Env
        events::on("env_info_result", [](const Event&) {
            YAM_LOG_DEBUG() << "env_info_result";
        });

        // Backtrace
        events::on("backtrace_result", [](const Event&) {
            YAM_LOG_DEBUG() << "backtrace_result";
        });

        // Embed
        events::on("agent_ready", [](const Event&) {
            YAM_LOG_INFO() << "agent_ready from embedded bridge";
        });

        // User scripts
        events::on("user_script_loaded", [](const Event& ev) {
            YAM_LOG_INFO() << "user_script_loaded " << ev.get_str("name")
                           << " ok=" << ev.get_bool("ok", false);
        });
        events::on("scripts_batch_done", [](const Event& ev) {
            YAM_LOG_INFO() << "scripts_batch_done ok=" << ev.get_i64("ok", 0)
                           << " fail=" << ev.get_i64("fail", 0);
        });
        events::on("script_failed", [](const Event& ev) {
            YAM_LOG_ERROR() << "script_failed " << ev.get_str("name")
                            << ": " << ev.get_str("error");
        });

        // Eval
        events::on("eval_result", [](const Event&) {
            YAM_LOG_DEBUG() << "eval_result";
        // --- Bridge internal events (routed to events:: by on_message) ---
        events::on("batch", [](const Event& ev) {
            YAM_LOG_DEBUG() << "batch received";
            (void)ev;
        });
        events::on("callback", [](const Event& ev) {
            YAM_LOG_DEBUG() << "callback";
            (void)ev;
        });
        events::on("console", [](const Event& ev) {
            // Handled separately by the JS console callback, but log here too.
            (void)ev;
        });
        events::on("reply", [](const Event& ev) {
            // Handled separately by JavaScriptBridge::on_reply.
            (void)ev;
        });

        // --- Additional bridge events ---
        events::on("user_scripts_list", [](const Event& ev) {
            YAM_LOG_INFO() << "user_scripts_list received";
            (void)ev;
        });
        events::on("trace_handles", [](const Event& ev) {
            YAM_LOG_DEBUG() << "trace_handles received";
            (void)ev;
        });
        events::on("class_probe", [](const Event& ev) {
            YAM_LOG_INFO() << "class_probe " << ev.get_str("className");
        });
        events::on("handle_inspect", [](const Event& ev) {
            YAM_LOG_DEBUG() << "handle_inspect";
            (void)ev;
        });
        events::on("snapshot", [](const Event& ev) {
            YAM_LOG_DEBUG() << "snapshot";
            (void)ev;
        });
        events::on("field_read", [](const Event& ev) {
            (void)ev;
        });
        events::on("field_write", [](const Event& ev) {
            (void)ev;
        });

        });
    });

    YAM_LOG_INFO() << "event router installed ("
                   << events::registered_types().size() << " event types)";
}

} // namespace yam
