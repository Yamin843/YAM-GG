// ===========================================================================
// yam_console.cpp — JS + C++ Console
// ===========================================================================

#include "yam.hpp"
#include "yam_internal.hpp"
#include "yam_c_api.hpp"
#include "yam_stalker.hpp"
#include "yam_java_model.hpp"

namespace yam {

// ===========================================================================
// SECTION 1 — JsConsole
// ===========================================================================

namespace console {

namespace {
JsConsole* g_js_inst = nullptr;
CppConsole* g_cpp_inst = nullptr;
std::mutex g_out_mu;
std::vector<String> g_console_buffer;
}

JsConsole::JsConsole() { g_js_inst = this; }
JsConsole::~JsConsole() { g_js_inst = nullptr; }

JsConsole& JsConsole::instance() {
    static JsConsole inst;
    return inst;
}

Result<void> JsConsole::open() {
    std::lock_guard<std::mutex> lk(mu_);
    if (open_) return Result<void>::ok();
    auto& bridge = JavaScriptBridge::instance();
    if (!bridge.is_ready()) {
        return Result<void>::err(ErrorCode::NotInitialized,
            "javascript bridge not ready");
    }
    bridge.set_console_callback([this](const String& level, const String& line) {
        {
            std::lock_guard<std::mutex> lk2(mu_);
            buffer_.push_back(level + ":" + line);
            if (buffer_.size() > 8192)
                buffer_.erase(buffer_.begin(), buffer_.begin() + 4096);
        }
        std::function<void(const String&, const String&)> s;
        { std::lock_guard<std::mutex> lk3(sink_mu_); s = sink_; }
        if (s) { try { s(level, line); } catch (...) {} }
        if (level == "error") YAM_LOG_ERROR() << "[js] " << line;
        else if (level == "warn") YAM_LOG_WARN() << "[js] " << line;
        else YAM_LOG_INFO() << "[js] " << line;
    });
    open_ = true;
    YAM_LOG_INFO() << "js console opened";
    return Result<void>::ok();
}

Result<void> JsConsole::close() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!open_) return Result<void>::ok();
    JavaScriptBridge::instance().set_console_callback(nullptr);
    open_ = false;
    return Result<void>::ok();
}

Result<EvalResult> JsConsole::eval(const String& code, i64 timeout_ms) {
    if (!open_)
        return Result<EvalResult>::err(ErrorCode::NotInitialized, "console closed");
    i64 t0 = time_util::now_ms();
    auto r = JavaScriptBridge::instance().eval(code);
    i64 dur = time_util::now_ms() - t0;
    (void)timeout_ms;
    if (!r) return Result<EvalResult>::err(r.error_code(), r.error_message());
    EvalResult er;
    er.duration_ms = dur;
    const auto& rep = r.value();
    if (rep.kind == "value" || rep.kind == "pong") {
        er.ok = true;
        er.result = rep.result;
    } else if (rep.kind == "null") {
        er.ok = true;
        er.result = "null";
    } else if (rep.kind == "error" || !rep.error.empty()) {
        er.ok = false;
        er.error = rep.error;
    } else {
        er.ok = rep.ok;
        er.result = rep.result;
    }
    return Result<EvalResult>::ok(std::move(er));
}

std::vector<String> JsConsole::take_output() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<String> out;
    out.swap(buffer_);
    return out;
}

void JsConsole::set_sink(std::function<void(const String&, const String&)> cb) {
    std::lock_guard<std::mutex> lk(sink_mu_);
    sink_ = std::move(cb);
}

// ===========================================================================
// CppConsole
// ===========================================================================

CppConsole::CppConsole() { g_cpp_inst = this; }
CppConsole::~CppConsole() { g_cpp_inst = nullptr; }

CppConsole& CppConsole::instance() {
    static CppConsole inst;
    return inst;
}

void CppConsole::register_command(const String& name, Command fn) {
    std::lock_guard<std::mutex> lk(mu_);
    commands_[name] = std::move(fn);
}
void CppConsole::unregister_command(const String& name) {
    std::lock_guard<std::mutex> lk(mu_);
    commands_.erase(name);
}
void CppConsole::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    commands_.clear();
}
std::vector<String> CppConsole::commands() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<String> out;
    out.reserve(commands_.size());
    for (auto& p : commands_) out.push_back(p.first);
    return out;
}
Result<String> CppConsole::call(const String& name, const std::vector<String>& args) {
    Command fn;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = commands_.find(name);
        if (it == commands_.end())
            return Result<String>::err(ErrorCode::MethodNotFound, name);
        fn = it->second;
    }
    try { return Result<String>::ok(fn(args)); }
    catch (const std::exception& e) {
        return Result<String>::err(ErrorCode::InternalError, e.what());
    }
}
Result<String> CppConsole::execute(const String& line) {
    std::vector<String> toks;
    String cur;
    bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (q) {
            if (c == '\\' && i + 1 < line.size()) cur.push_back(line[++i]);
            else if (c == '"') q = false;
            else cur.push_back(c);
        } else {
            if (c == '"') q = true;
            else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            } else cur.push_back(c);
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    if (toks.empty()) return Result<String>::ok("");
    String cmd = toks.front();
    toks.erase(toks.begin());
    return call(cmd, toks);
}

// ---------------------------------------------------------------------------
// Built-ins — ALL bridge actions covered with real calls
// ---------------------------------------------------------------------------

void CppConsole::install_builtins() {
    // Basic
    register_command("help", [this](const std::vector<String>&) -> String {
        auto all = commands();
        std::sort(all.begin(), all.end());
        String s = "Commands (" + std::to_string(all.size()) + "):\n";
        for (auto& c : all) s += "  " + c + "\n";
        return s;
    });

    register_command("state", [](const std::vector<String>&) -> String {
        return RuntimeDiag::to_string(RuntimeDiag::snapshot());
    });
    register_command("state_json", [](const std::vector<String>&) -> String {
        return RuntimeDiag::to_json(RuntimeDiag::snapshot());
    });

    // Bridge
    register_command("ping", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().ping();
        return r ? ("pong id=" + std::to_string(r.value().id)) : "ping failed";
    });

    register_command("eval", [](const std::vector<String>& args) -> String {
        if (args.empty()) return "usage: eval <js-code>";
        String code;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) code += " ";
            code += args[i];
        }
        auto r = JavaScriptBridge::instance().eval(code);
        if (!r) return "error: " + r.error_message();
        return r.value().result;
    });

    register_command("js", [](const std::vector<String>& args) -> String {
        return CppConsole::instance().call("eval", args).value_or("error");
    });


    // Registry
    register_command("reg", [](const std::vector<String>& args) -> String {
        if (args.empty() || args[0] == "list") {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%zu entries, %zu bytes",
                          registry().size(), registry().total_bytes());
            return buf;
        }
        if (args[0] == "clear") { registry().clear(); return "cleared"; }
        return "usage: reg [list|clear]";
    });
    register_command("gc", [](const std::vector<String>&) -> String {
        usize b = registry().size();
        Gc::collect();
        return "gc: " + std::to_string(b) + " -> " +
               std::to_string(registry().size());
    });
    register_command("handles", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().list_handles();
        return r ? r.value().result : "error";
    });
    register_command("handle_budget", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().get_handle_budget();
        return r ? r.value().result : "error";
    });

    // Memory / Module / Symbol
    register_command("modules", [](const std::vector<String>& a) -> String {
        auto mods = Module::enumerate();
        String f = a.empty() ? "" : a[0];
        String s;
        for (auto& m : mods) {
            if (!f.empty() && m.name().find(f) == String::npos) continue;
            s += m.name() + "  base=" + str::hex(m.base()) +
                 "  size=0x" + str::hex(m.size()) + "\n";
        }
        return s;
    });
    register_command("exports", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: exports <module>";
        auto m = Module::find(a[0]);
        if (!m) return "module not found: " + a[0];
        String s;
        size_t n = 0;
        for (auto& e : m.value().exports()) {
            s += e.name + "  " + str::hex(e.address) + "\n";
            if (++n > 500) { s += "...\n"; break; }
        }
        return s;
    });
    register_command("sym", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: sym <name>";
        void* p = Symbol::resolve(a[0]);
        if (!p) return "not found";
        return a[0] + " = " + str::hex(p);
    });
    register_command("scan", [](const std::vector<String>& a) -> String {
        if (a.size() < 3) return "usage: scan <hex-base> <size> <pattern>";
        u64 base = std::strtoull(a[0].c_str(), nullptr, 16);
        u64 sz = std::strtoull(a[1].c_str(), nullptr, 0);
        auto r = Memory::scan(reinterpret_cast<void*>(base), sz, a[2]);
        if (!r) return "error: " + r.error_message();
        String s = std::to_string(r.value().size()) + " hits\n";
        for (size_t i = 0; i < r.value().size() && i < 100; ++i)
            s += "  " + str::hex(r.value()[i]) + "\n";
        return s;
    });
    register_command("read_mem", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: read_mem <hex-addr> <size>";
        u64 addr = std::strtoull(a[0].c_str(), nullptr, 16);
        u64 sz = std::strtoull(a[1].c_str(), nullptr, 0);
        auto r = Memory::read_bytes(reinterpret_cast<void*>(addr), sz);
        if (!r) return "error";
        String s;
        char buf[8];
        for (u8 b : r.value()) {
            std::snprintf(buf, sizeof(buf), "%02x ", b);
            s += buf;
        }
        return s;
    });
    register_command("mem_info", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: mem_info <hex-addr>";
        u64 addr = std::strtoull(a[0].c_str(), nullptr, 16);
        auto m = Module::find_by_address(reinterpret_cast<void*>(addr));
        if (!m) return "not in module";
        return m.value().name() + " base=" + str::hex(m.value().base());
    });

    // Stalker
    register_command("stalker_follow", [](const std::vector<String>&) -> String {
        auto r = stalker().follow_me();
        return r ? "following" : ("error: " + r.error_message());
    });
    register_command("stalker_unfollow", [](const std::vector<String>&) -> String {
        auto r = stalker().unfollow_me();
        return r ? "unfollowed" : "error";
    });
    register_command("stalker_exclude", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: stalker_exclude <hex-addr> <size>";
        u64 addr = std::strtoull(a[0].c_str(), nullptr, 16);
        u64 sz = std::strtoull(a[1].c_str(), nullptr, 0);
        auto r = stalker().exclude(reinterpret_cast<void*>(addr), sz);
        return r ? "excluded" : "error";
    });
    register_command("stalker_flush", [](const std::vector<String>&) -> String {
        stalker().flush();
        return "flushed";
    });
    register_command("stalker_gc", [](const std::vector<String>&) -> String {
        stalker().garbage_collect();
        return "gc done";
    });
    register_command("stalker_supported", [](const std::vector<String>&) -> String {
        return Stalker::is_supported() ? "yes" : "no";
    });

    // Java
    register_command("classes", [](const std::vector<String>& a) -> String {
        auto r = JavaScriptBridge::instance().enumerate_classes();
        if (!r) return "error: " + r.error_message();
        String f = a.empty() ? "" : a[0];
        auto list = detail::json::strings_of_array(r.value().result);
        String s;
        size_t n = 0;
        for (auto& c : list) {
            if (!f.empty() && c.find(f) == String::npos) continue;
            s += c + "\n";
            if (++n > 500) break;
        }
        return s;
    });
    register_command("loaders", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().enumerate_loaders();
        if (!r) return "error";
        auto list = detail::json::strings_of_array(r.value().result);
        String s;
        for (auto& l : list) s += l + "\n";
        return s;
    });
    register_command("use", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: use <class>";
        auto r = JavaScriptBridge::instance().use_class(a[0]);
        return r ? ("handle=" + std::to_string(r.value().handle)) : "error";
    });
    register_command("choose", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: choose <class>";
        size_t count = 0;
        auto r = JavaFacade::choose(a[0], [&count](Handle h) -> int {
            ++count;
            (void)h;
            return 0;
        });
        return r ? ("matched " + std::to_string(count)) : ("error: " + r.error_message());
    });
    register_command("probe", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: probe <class>";
        auto r = JavaScriptBridge::instance().probe_class(a[0]);
        return r ? r.value().result : "error";
    });
    register_command("own_members", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: own_members <class>";
        auto r = JavaScriptBridge::instance().list_own(a[0]);
        return r ? r.value().result : "error";
    });
    register_command("overloads", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: overloads <class> <method>";
        auto r = JavaScriptBridge::instance().list_overloads(a[0], a[1]);
        return r ? r.value().result : "error";
    });

    // Tracing
    register_command("trace", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: trace <class> <method> [sig]";
        auto r = trace::add(a[0], a[1], a.size() > 2 ? a[2] : "", 0, 1, 0);
        return r ? ("trace id=" + std::to_string(r.value())) :
                   ("error: " + r.error_message());
    });
    register_command("trace_class", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: trace_class <class> [pattern]";
        auto r = trace::add_class(a[0], a.size() > 1 ? a[1] : "*", 0, 1, 0);
        return r ? ("trace id=" + std::to_string(r.value())) : "error";
    });
    register_command("untrace", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: untrace <id>";
        u64 id = std::strtoull(a[0].c_str(), nullptr, 10);
        auto r = trace::remove(id);
        return r ? "removed" : "error";
    });
    register_command("clear_traces", [](const std::vector<String>&) -> String {
        auto r = trace::clear();
        return r ? "cleared" : "error";
    });
    register_command("pause", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: pause <id>";
        auto r = trace::pause(std::strtoull(a[0].c_str(), nullptr, 10));
        return r ? "paused" : "error";
    });
    register_command("resume", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: resume <id>";
        auto r = trace::resume(std::strtoull(a[0].c_str(), nullptr, 10));
        return r ? "resumed" : "error";
    });

    // Breakpoints
    register_command("bp", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: bp <trace-id> [on|off]";
        u64 id = std::strtoull(a[0].c_str(), nullptr, 10);
        bool on = a.size() < 2 || a[1] == "on";
        auto r = bp::set(id, on, 1);
        return r ? "updated" : "error";
    });
    register_command("bp_continue", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: bp_continue <bp-id>";
        auto r = bp::continue_bp(std::strtoull(a[0].c_str(), nullptr, 10));
        return r ? "continued" : "error";
    });
    register_command("bp_throw", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: bp_throw <bp-id> <message>";
        auto r = bp::throw_bp(std::strtoull(a[0].c_str(), nullptr, 10), a[1]);
        return r ? "thrown" : "error";
    });
    register_command("bp_cancel_all", [](const std::vector<String>&) -> String {
        bp::cancel_all();
        return "cancelled";
    });

    // Params
    register_command("set_param", [](const std::vector<String>& a) -> String {
        if (a.size() < 4) return "usage: set_param <trace-id> <index> <type> <value>";
        auto r = params::set(
            std::strtoull(a[0].c_str(), nullptr, 10),
            std::atoi(a[1].c_str()), a[2], a[3]);
        return r ? "set" : "error";
    });
    register_command("clear_param", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: clear_param <trace-id> <index>";
        auto r = params::clear(
            std::strtoull(a[0].c_str(), nullptr, 10),
            std::atoi(a[1].c_str()));
        return r ? "cleared" : "error";
    });

    // Watches
    register_command("watch", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: watch <handle-id> <field>";
        u64 h = std::strtoull(a[0].c_str(), nullptr, 10);
        auto r = watch::start(h, a[1], 250);
        return r ? "watching" : "error";
    });
    register_command("unwatch", [](const std::vector<String>& a) -> String {
        if (a.empty()) { watch::stop_all(); return "all stopped"; }
        auto r = watch::stop(std::strtoull(a[0].c_str(), nullptr, 10));
        return r ? "stopped" : "error";
    });

    // User scripts
    register_command("run_script", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: run_script <name> <js>";
        String code;
        for (size_t i = 1; i < a.size(); ++i) {
            if (i > 1) code += " ";
            code += a[i];
        }
        auto r = user_scripts::run(a[0], code);
        return r ? ("loaded " + a[0]) : ("error: " + r.error_message());
    });

    // Extensions / Agent
register_command("agent_emit", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: agent_emit <event> [payload-json]";
        auto r = JavaScriptBridge::instance().agent_emit(
            a[0], a.size() > 1 ? a[1] : "{}");
        return r ? "emitted" : "error";
    });

    // Native hooks
    register_command("hook_native", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: hook_native <addr> <js>";
        auto r = JavaScriptBridge::instance().hook_native(a[0], a[1], "");
        return r ? "hooked" : "error";
    });
    register_command("hook_dex_loader", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().hook_dex_loader();
        return r ? "hooked" : "error";
    });
    register_command("hook_load_class", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().hook_load_class();
        return r ? "hooked" : "error";
    });
    register_command("hook_load_library", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().hook_load_library();
        return r ? "hooked" : "error";
    });
    register_command("hook_app_oncreate", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().hook_app_oncreate();
        return r ? "hooked" : "error";
    });

    // Deopt
register_command("deopt_all", [](const std::vector<String>&) -> String {        auto r = JavaScriptBridge::instance().deopt_everything();
        return r ? "deopted" : "error";
    });
    register_command("deopt_boot", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().deopt_boot_image();
        return r ? "deopted" : "error";
    });

        // --- Advanced subsystems ---
    register_command("cmodule", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: cmodule <name> <source>";
        String src;
        for (size_t i = 1; i < a.size(); ++i) {
            if (i > 1) src += " ";
            src += a[i];
        }
        auto m = std::make_shared<CModule>(src, a[0]);
        if (!m->valid()) return "cmodule create failed";
        auto lr = m->link();
        if (!lr) return "link failed: " + lr.error_message();
        auto rr = cmodules().add(a[0], m);
        if (!rr) return "registry error: " + rr.error_message();
        String s = "cmodule " + a[0] + " linked\n";
        for (auto& sym : m->symbols())
            s += "  " + sym.first + " -> " + str::hex(sym.second) + "\n";
        return s;
    });

    register_command("cloak_add_range", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: cloak_add_range <hex-addr> <size>";
        u64 addr = std::strtoull(a[0].c_str(), nullptr, 16);
        u64 sz = std::strtoull(a[1].c_str(), nullptr, 0);
        Cloak::add_range(reinterpret_cast<void*>(addr), sz);
        return "added";
    });
    register_command("cloak_has_range", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: cloak_has_range <hex-addr>";
        u64 addr = std::strtoull(a[0].c_str(), nullptr, 16);
        return Cloak::has_range_containing(reinterpret_cast<void*>(addr))
            ? "yes" : "no";
    });
    register_command("cloak_add_thread", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: cloak_add_thread <tid>";
        u32 tid = static_cast<u32>(std::strtoul(a[0].c_str(), nullptr, 10));
        Cloak::add_thread(tid);
        return "added";
    });
    register_command("cloak_ranges", [](const std::vector<String>&) -> String {
        auto r = Cloak::enumerate_ranges();
        String s = std::to_string(r.size()) + " ranges\n";
        for (auto& x : r) s += "  " + str::hex(x.base) + " size=" +
                                std::to_string(x.size) + "\n";
        return s;
    });

    register_command("threads", [](const std::vector<String>&) -> String {
        auto ts = ThreadRegistry::enumerate();
        String s = std::to_string(ts.size()) + " threads\n";
        for (auto& t : ts)
            s += "  tid=" + std::to_string(t.id) +
                 " name=" + t.name +
                 " state=" + std::to_string(t.state) + "\n";
        return s;
    });

    register_command("api_resolve", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: api_resolve <query>";
        ApiResolver r(ApiResolver::Type::Module);
        if (!r.valid()) return "resolver not available";
        auto matches = r.enumerate_matches(a[0]);
        if (!matches) return "error: " + matches.error_message();
        String s = std::to_string(matches.value().size()) + " matches\n";
        for (auto& m : matches.value())
            s += "  " + m.name + " @ " + str::hex(m.address) +
                 " (" + "" + ")\n";
        return s;
    });

    register_command("elf_info", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: elf_info <path>";
        auto r = ElfModule::from_file(a[0]);
        if (!r) return "error: " + r.error_message();
        auto& m = r.value();
        String s;
        s += "base=" + str::hex(m.base_address()) + "\n";
        s += "size=" + std::to_string(m.mapped_size()) + "\n";
        s += "entry=" + str::hex(m.entrypoint()) + "\n";
        s += "interpreter=" + m.interpreter() + "\n";
        s += "sections=" + std::to_string(m.sections().size()) + "\n";
        s += "symbols=" + std::to_string(m.symbols().size()) + "\n";
        s += "segments=" + std::to_string(m.segments().size()) + "\n";
        return s;
    });

    register_command("writer_test", [](const std::vector<String>&) -> String {
        // Quick self-test of the arm64 writer.
        ByteVector buf(64, 0);
        Arm64Writer w(buf.data());
        if (!w.valid()) return "writer not available";
        w.put_nop();
        w.put_ret();
        w.flush();
        char out[64];
        std::snprintf(out, sizeof(out), "wrote %u bytes: %02x %02x %02x %02x",
            w.offset(), buf[0], buf[1], buf[2], buf[3]);
        return out;
    });    register_command("internals", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().get_internals();
        return r ? r.value().result : "error";
    });

    register_command("backtrace", [](const std::vector<String>& a) -> String {
        i32 limit = a.empty() ? 8 : std::atoi(a[0].c_str());
        auto r = JavaScriptBridge::instance().backtrace(limit);
        return r ? r.value().result : "error";
    });

    // --- ThreadRegistry / Linux ---
    register_command("thread_info", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: thread_info <tid>";
        u32 tid = static_cast<u32>(std::strtoul(a[0].c_str(), nullptr, 10));
        auto r = ThreadRegistry::find(tid);
        if (!r) return "not found";
        auto& t = r.value();
        String s = "tid=" + std::to_string(t.id) + "\n";
        s += "name=" + t.name + "\n";
        s += "state=" + std::to_string(t.state) + "\n";
        s += "flags=" + std::to_string(t.flags) + "\n";
        return s;
    });

    // --- Trackers ---
    register_command("alloc_track", [](const std::vector<String>&) -> String {
        AllocationTracker t;
        if (!t.valid()) return "not available";
        t.begin(1);
        // (User code does work here)
        t.end();
        String s = "blocks=" + std::to_string(t.block_count()) + "\n";
        s += "total=" + std::to_string(t.block_total_size()) + "\n";
        return s;
    });

    register_command("instance_track", [](const std::vector<String>&) -> String {
        InstanceTracker t;
        if (!t.valid()) return "not available";
        t.begin(1);
        t.end();
        return "instances=" + std::to_string(t.total_count());
    });

    // --- EventSink ---
    register_command("event_sink_mask", [](const std::vector<String>&) -> String {
        EventSink s;
        if (!s.valid()) return "not available";
        return "mask=" + std::to_string(s.query_mask());
    });

    // --- TLS ---
    register_command("tls_test", [](const std::vector<String>&) -> String {
        TlsKey k;
        if (!k.valid()) return "tls not available";
        k.set(reinterpret_cast<void*>(0x1234));
        void* v = k.get();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "tls: got %p", v);
        return buf;
    });

    // --- Kernel ---
    register_command("kernel_available", [](const std::vector<String>&) -> String {
        return Kernel::available() ? "yes" : "no";
    });
    register_command("kernel_modules", [](const std::vector<String>&) -> String {
        auto m = Kernel::enumerate_modules();
        String s = std::to_string(m.size()) + " modules\n";
        for (size_t i = 0; i < m.size() && i < 50; ++i) s += "  " + m[i] + "\n";
        return s;
    });

    // --- ELF ---
    register_command("elf_sections", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: elf_sections <path>";
        auto r = ElfModule::from_file(a[0]);
        if (!r) return "error: " + r.error_message();
        auto sections = r.value().sections();
        String s = std::to_string(sections.size()) + " sections\n";
        for (size_t i = 0; i < sections.size() && i < 30; ++i)
            s += "  " + sections[i].name + "\n";
        return s;
    });
    register_command("elf_symbols", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: elf_symbols <path>";
        auto r = ElfModule::from_file(a[0]);
        if (!r) return "error: " + r.error_message();
        auto syms = r.value().symbols();
        String s = std::to_string(syms.size()) + " symbols\n";
        for (size_t i = 0; i < syms.size() && i < 30; ++i)
            s += "  " + syms[i].name + " @ " + str::hex(syms[i].address) + "\n";
        return s;
    });

    // --- Stalker helpers ---
    register_command("stalker_set_trust", [](const std::vector<String>& a) -> String {
        int t = a.empty() ? 0 : std::atoi(a[0].c_str());
        stalker().set_trust_threshold(t);
        return "trust=" + std::to_string(stalker().get_trust_threshold());
    });

    // --- CModule ---
    register_command("cmodule_syms", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: cmodule_syms <name>";
        auto m = cmodules().find(a[0]);
        if (!m) return "cmodule not found: " + a[0];
        auto syms = m->symbols();
        String s = "cmodule " + a[0] + " (" + std::to_string(syms.size()) + " syms)\n";
        for (auto& sym : syms)
            s += "  " + sym.first + " -> " + str::hex(sym.second) + "\n";
        return s;
    });

    register_command("cmodules", [](const std::vector<String>&) -> String {
        auto names = cmodules().names();
        String s = std::to_string(names.size()) + " cmodules\n";
        for (auto& n : names) s += "  " + n + "\n";
        return s;
    });

    register_command("cmodule_free", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: cmodule_free <name>";
        return cmodules().remove(a[0]) ? "removed" : "not found";
    });

    // --- Cloak ---
    register_command("cloak_fds", [](const std::vector<String>&) -> String {
        auto fds = Cloak::enumerate_fds();
        String s = std::to_string(fds.size()) + " hidden fds\n";
        for (auto fd : fds) s += "  " + std::to_string(fd) + "\n";
        return s;
    });

    // --- ApiResolver ---
    register_command("api_modules", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: api_modules <query>";
        ApiResolver r(ApiResolver::Type::Module);
        if (!r.valid()) return "not available";
        auto matches = r.enumerate_matches(a[0]);
        if (!matches) return "error";
        String s = std::to_string(matches.value().size()) + " matches\n";
        for (size_t i = 0; i < matches.value().size() && i < 30; ++i)
            s += "  " + matches.value()[i].name + "\n";
        return s;
    });

    // --- Config ---
    register_command("set_log_level", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: set_log_level <trace|debug|info|warn|error|off>";
        LogLevel lv = LogLevel::Info;
        if (a[0] == "trace") lv = LogLevel::Trace;
        else if (a[0] == "debug") lv = LogLevel::Debug;
        else if (a[0] == "info") lv = LogLevel::Info;
        else if (a[0] == "warn") lv = LogLevel::Warn;
        else if (a[0] == "error") lv = LogLevel::Error;
        else if (a[0] == "off") lv = LogLevel::Off;
        Log::set_level(lv);
        return "log level set";
    });

    register_command("set_bp_timeout", [](const std::vector<String>& a) -> String {
        i64 ms = a.empty() ? 300000 : std::atoll(a[0].c_str());
        bp::set_timeout(ms);
        return "bp timeout=" + std::to_string(ms) + "ms";
    });

    register_command("bridge_state", [](const std::vector<String>&) -> String {
        String s = "ready=" + String(JavaScriptBridge::instance().is_ready() ? "yes" : "no") + "\n";
        s += "commands=" + std::to_string(CppConsole::instance().commands().size()) + "\n";
        s += "events=" + std::to_string(events::registered_types().size()) + "\n";
        s += "traces=" + std::to_string(trace::active().size()) + "\n";
        return s;
    });

    register_command("throwable_stacktrace", [](const std::vector<String>& a) -> String {
        bool on = !a.empty() && (a[0] == "on" || a[0] == "1");
        auto r = JavaScriptBridge::instance().set_throwable_stacktrace(on);
        return r ? ("enabled=" + String(on ? "true" : "false")) : "error";
    });

    register_command("clear_probe_cache", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().clear_probe_cache();
        return r ? "cleared" : "error";
    });

    register_command("repl_async", [](const std::vector<String>& a) -> String {
        bool on = !a.empty() && (a[0] == "on" || a[0] == "1");
        auto r = JavaScriptBridge::instance().set_repl_async_enabled(on);
        return r ? ("async=" + String(on ? "on" : "off")) : "error";
    });

    // --- Event stats ---
    register_command("events", [](const std::vector<String>&) -> String {
        auto types = events::registered_types();
        String s = std::to_string(types.size()) + " event types\n";
        for (auto& t : types) s += "  " + t + "\n";
        s += "total dispatched: " + std::to_string(events::count()) + "\n";
        return s;
    });

    // --- JavaModel helpers ---
    register_command("java_use", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: java_use <class>";
        auto r = java::use(a[0]);
        if (!r) return "error: " + r.error_message();
        return "handle=" + std::to_string(r.value()->handle());
    });

    register_command("java_method", [](const std::vector<String>& a) -> String {
        if (a.size() < 2) return "usage: java_method <class> <name> [sig]";
        auto c = java::use(a[0]);
        if (!c) return "error: " + c.error_message();
        auto m = c.value()->method(a[1], a.size() > 2 ? a[2] : "");
        if (!m) return "error: " + m.error_message();
        return "method handle=" + std::to_string(m.value()->handle());
    });

    register_command("java_new", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: java_new <class> [args-json]";
        auto c = java::use(a[0]);
        if (!c) return "error: " + c.error_message();
        std::vector<JsonValue> args;
        if (a.size() > 1) {
            auto p = JsonValue::parse(a[1]);
            if (p && p.value().is_arr()) args = p.value().arr_val;
        }
        auto i = c.value()->create(args);
        if (!i) return "error: " + i.error_message();
        return "instance handle=" + std::to_string(i.value()->handle());
    });

    register_command("java_snapshot", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: java_snapshot <handle-id>";
        u64 hid = std::strtoull(a[0].c_str(), nullptr, 10);
        auto& b = JavaScriptBridge::instance();
        auto r = b.inspect_handle(hid);
        if (!r) return "error: " + r.error_message();
        return r.value().result;
    });

    register_command("java_deopt", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: java_deopt <class|all|boot>";
        if (a[0] == "all") {
            auto r = java::deoptimize_everything();
            return r ? "deopted all" : "error";
        }
        if (a[0] == "boot") {
            auto r = java::deoptimize_boot_image();
            return r ? "deopted boot" : "error";
        }
        auto r = java::deoptimize_class(a[0]);
        return r ? "deopted" : "error";
    });

    // --- Subsystem console commands ---
    register_command("cloak_add_fd", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: cloak_add_fd <fd>";
        Cloak::add_fd(std::atoi(a[0].c_str()));
        return "added";
    });
    register_command("cloak_has_fd", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: cloak_has_fd <fd>";
        return Cloak::has_fd(std::atoi(a[0].c_str())) ? "yes" : "no";
    });

    register_command("api_resolve_module", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: api_resolve_module <query>";
        ApiResolver r(ApiResolver::Type::Module);
        if (!r.valid()) return "resolver not available";
        auto m = r.enumerate_matches(a[0]);
        if (!m) return "error: " + m.error_message();
        String s = std::to_string(m.value().size()) + " matches\n";
        for (auto& x : m.value())
            s += "  " + x.name + " @ " + str::hex(x.address) + "\n";
        return s;
    });

    register_command("apk_resolve", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: apk_resolve <query>";
        ApiResolver r(ApiResolver::Type::Default);
        if (!r.valid()) return "resolver not available";
        auto m = r.enumerate_matches(a[0]);
        if (!m) return "error: " + m.error_message();
        String s = std::to_string(m.value().size()) + " matches\n";
        for (auto& x : m.value())
            s += "  " + x.name + " @ " + str::hex(x.address) + "\n";
        return s;
    });

    register_command("bounds_attach", [](const std::vector<String>& a) -> String {
        BoundsChecker b;
        if (!b.valid()) return "not available";
        auto r = b.attach(a.empty() ? "" : a[0]);
        return r ? "attached" : "error";
    });

    register_command("alloc_probe_attach", [](const std::vector<String>& a) -> String {
        AllocatorProbe p;
        if (!p.valid()) return "not available";
        auto r = p.attach(a.empty() ? "" : a[0]);
        return r ? "attached" : "error";
    });

    register_command("exceptor_mode", [](const std::vector<String>& a) -> String {
        Exceptor e;
        if (!e.valid()) return "not available";
        i32 mode = a.empty() ? 0 : std::atoi(a[0].c_str());
        auto r = e.set_mode(mode);
        return r ? "mode set" : "error";
    });

    register_command("sourcemap_resolve", [](const std::vector<String>& a) -> String {
        if (a.size() < 3) return "usage: sourcemap_resolve <line> <col> <json-path>";
        // Read json file
        std::ifstream f(a[2]);
        if (!f) return "cannot open file";
        std::stringstream ss; ss << f.rdbuf();
        SourceMap sm(ss.str());
        if (!sm.valid()) return "sourcemap invalid";
        auto p = sm.resolve(std::atoi(a[0].c_str()), std::atoi(a[1].c_str()));
        if (!p) return "error: " + p.error_message();
        return "line=" + std::to_string(p.value().line) +
               " col=" + std::to_string(p.value().column) +
               " name=" + p.value().name +
               " src=" + p.value().source;
    });

    register_command("tls_key_test", [](const std::vector<String>&) -> String {
        TlsKey k;
        if (!k.valid()) return "not available";
        k.set(reinterpret_cast<void*>(0x12345678));
        void* v = k.get();
        return v == reinterpret_cast<void*>(0x12345678) ? "ok" : "mismatch";
    });

    register_command("events_count", [](const std::vector<String>&) -> String {
        return "total events: " + std::to_string(events::count());
    });

    register_command("chunks", [](const std::vector<String>&) -> String {
        auto a = chunks::active();
        String s = std::to_string(a.size()) + " active chunk sessions\n";
        for (auto& c : a)
            s += "  #" + std::to_string(c.id) + " kind=" + c.kind +
                 " recv=" + std::to_string(c.received) + "/" +
                 std::to_string(c.total) + "\n";
        return s;
    });

    register_command("traces", [](const std::vector<String>&) -> String {
        auto t = trace::active();
        String s = std::to_string(t.size()) + " active traces\n";
        for (auto& x : t)
            s += "  #" + std::to_string(x.trace_id) + " " +
                 x.class_name + "::" + x.method_name +
                 (x.paused ? " [paused]" : "") + "\n";
        return s;
    });

    register_command("user_scripts", [](const std::vector<String>&) -> String {
        auto l = user_scripts::list();
        String s = std::to_string(l.size()) + " loaded scripts\n";
        for (auto& x : l)
            s += "  " + x.name + " (" + std::to_string(x.size) +
                 " bytes, ok=" + (x.ok ? "yes" : "no") + ")\n";
        return s;
    });

    register_command("watches", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().watch_list();
        if (!r) return "error: " + r.error_message();
        return "watch_list sent (results as event)";
    });

    // --- Header/Tail helpers ---
    register_command("log_level", [](const std::vector<String>& a) -> String {
        if (a.empty()) {
            auto lv = Log::level();
            const char* names[] = {"trace","debug","info","warn","error","fatal","off"};
            int i = static_cast<int>(lv);
            return i >= 0 && i < 7 ? names[i] : "unknown";
        }
        LogLevel lv = LogLevel::Info;
        if (a[0] == "trace") lv = LogLevel::Trace;
        else if (a[0] == "debug") lv = LogLevel::Debug;
        else if (a[0] == "info") lv = LogLevel::Info;
        else if (a[0] == "warn") lv = LogLevel::Warn;
        else if (a[0] == "error") lv = LogLevel::Error;
        else if (a[0] == "off") lv = LogLevel::Off;
        Log::set_level(lv);
        return "log_level set";
    });

    register_command("registry_query", [](const std::vector<String>& a) -> String {
        auto snap = registry().snapshot();
        String filter = a.empty() ? "" : a[0];
        String s = std::to_string(snap.size()) + " entries\n";
        size_t count = 0;
        for (auto& e : snap) {
            if (!e) continue;
            if (!filter.empty()) {
                String cls = e->class_name;
                if (cls.find(filter) == String::npos) continue;
            }
            s += "  id=" + std::to_string(e->id) +
                 " kind=" + std::to_string(static_cast<int>(e->kind)) +
                 " class=" + e->class_name + "\n";
            if (++count > 50) { s += "  ...\n"; break; }
        }
        return s;
    });

    register_command("range", [](const std::vector<String>& a) -> String {
        auto all = Module::enumerate();
        String filter = a.empty() ? "" : a[0];
        String s = std::to_string(all.size()) + " ranges\n";
        size_t n = 0;
        for (auto& r : all) {
            if (!filter.empty() && r.path().find(filter) == String::npos) continue;
            s += "  " + str::hex(r.base()) + " size=0x" + str::hex(r.size()) +
                 " " + r.path() + "\n";
            if (++n > 50) { s += "  ...\n"; break; }
        }
        return s;
    });

    register_command("elf", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: elf <path>";
        auto r = ElfModule::from_file(a[0]);
        if (!r) return "error: " + r.error_message();
        auto& m = r.value();
        String s;
        s += "base=" + str::hex(m.base_address()) + "\n";
        s += "size=" + std::to_string(m.mapped_size()) + "\n";
        s += "entry=" + str::hex(m.entrypoint()) + "\n";
        s += "interpreter=" + m.interpreter() + "\n";
        s += "sections=" + std::to_string(m.sections().size()) + "\n";
        s += "symbols=" + std::to_string(m.symbols().size()) + "\n";
        return s;
    });

    register_command("stalker_status", [](const std::vector<String>&) -> String {
        auto& s = stalker();
        String out;
        out += "supported=" + String(Stalker::is_supported() ? "yes" : "no") + "\n";
        out += "following_me=" + String(s.is_following_me() ? "yes" : "no") + "\n";
        out += "trust_threshold=" + std::to_string(s.get_trust_threshold()) + "\n";
        return out;
    });

YAM_LOG_INFO() << "cpp console: " << commands().size() << " commands";


    register_command("deopt_class", [](const std::vector<String>& a) -> String {
        if (a.empty()) return "usage: deopt_class <class>";
        auto r = JavaScriptBridge::instance().deopt_class(a[0]);
        return r ? "deopted" : "error";
    });

    register_command("env", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().env_info();
        if (!r) return "error: " + r.error_message();
        return r.value().result;
    });

    register_command("extensions", [](const std::vector<String>&) -> String {
        auto r = JavaScriptBridge::instance().list_extensions();
        return r ? r.value().result : "error";
    });
}

// ===========================================================================
// Console (unified)
// ===========================================================================

Console& Console::instance() {
    static Console inst;
    return inst;
}
Result<void> Console::start() {
    if (running_) return Result<void>::ok();
    CppConsole::instance().install_builtins();
    auto r = JsConsole::instance().open();
    if (!r) YAM_LOG_WARN() << "console: js part not open: " << r.error_message();
    running_ = true;
    return Result<void>::ok();
}
Result<void> Console::stop() {
    if (!running_) return Result<void>::ok();
    JsConsole::instance().close();
    running_ = false;
    return Result<void>::ok();
}
JsConsole& Console::js() { return JsConsole::instance(); }
CppConsole& Console::cpp() { return CppConsole::instance(); }

} // namespace console

// ===========================================================================
// Entry / FinalGlue / YAM
// ===========================================================================

namespace {
std::atomic<bool> g_entry_started{false};
EntryOptions g_entry_opts;
std::mutex g_entry_mu;
}

Result<void> Entry::start(const EntryOptions& opt) {
    std::lock_guard<std::mutex> lk(g_entry_mu);
    if (g_entry_started.load())
        return Result<void>::err(ErrorCode::AlreadyInitialized, "entry");
    g_entry_opts = opt;
    Log::set_level(opt.log_level);
    YAM_LOG_INFO() << "yam entry: starting";

    RuntimeOptions ro;
    ro.log_level = opt.log_level;
    ro.script_name = opt.script_name;
    ro.enable_debugger = opt.enable_debugger;
    ro.worker_threads = opt.worker_threads;
    ro.backend = opt.backend;

    auto rr = Runtime::instance().init(ro);
    if (!rr) {
        YAM_LOG_ERROR() << "runtime init failed: " << rr.error_message();
        return rr;
    }

    // Obtain the C++ interceptor (singleton).
    Interceptor::instance();

    if (opt.enable_java) {
        auto jr = JavaFacade::start();
        if (!jr) YAM_LOG_WARN() << "java init: " << jr.error_message();
        else install_event_router();
    }

    if (opt.enable_console) {
        auto cr = console::Console::instance().start();
        if (!cr) YAM_LOG_WARN() << "console init: " << cr.error_message();
    }

    g_entry_started.store(true);
    YAM_LOG_INFO() << "yam entry: ready";
    return Result<void>::ok();
}

Result<void> Entry::stop() {
    std::lock_guard<std::mutex> lk(g_entry_mu);
    if (!g_entry_started.load()) return Result<void>::ok();
    YAM_LOG_INFO() << "yam entry: stopping";

    if (g_entry_opts.enable_console) console::Console::instance().stop();
    if (g_entry_opts.enable_java) JavaFacade::stop();
    Gc::collect();
    registry().clear();
    Runtime::instance().shutdown();

    g_entry_started.store(false);
    return Result<void>::ok();
}
bool Entry::started() { return g_entry_started.load(); }
const EntryOptions& Entry::options() { return g_entry_opts; }

namespace { std::atomic<bool> g_final{false}; }

Result<void> FinalGlue::initialize_all(const EntryOptions& opt) {
    if (g_final.load()) return Result<void>::err(ErrorCode::AlreadyInitialized, "");
    auto er = Entry::start(opt);
    if (!er) return er;
    g_final.store(true);
    return Result<void>::ok();
}
Result<void> FinalGlue::shutdown_all() {
    if (!g_final.load()) return Result<void>::ok();
    Entry::stop();
    g_final.store(false);
    return Result<void>::ok();
}
bool FinalGlue::initialized() { return g_final.load(); }

Result<void> YAM::init(const EntryOptions& opt) { return FinalGlue::initialize_all(opt); }
Result<void> YAM::shutdown() { return FinalGlue::shutdown_all(); }
bool YAM::started() { return FinalGlue::initialized(); }

} // namespace yam
