// ===========================================================================
// yam_java_model.cpp — C++ object model for Java objects
// ===========================================================================

#include "yam_java_model.hpp"
#include "yam_internal.hpp"

namespace yam {

// ===========================================================================
// SECTION 1 — Helpers
// ===========================================================================

namespace {
String build_args_json(const std::vector<JsonValue>& args) {
    JsonValue arr;
    arr.type = JsonValue::Type::Array;
    for (auto& a : args) arr.arr_val.push_back(a);
    return arr.stringify();
}

String handles_to_json(const std::vector<u64>& handles) {
    JsonValue arr;
    arr.type = JsonValue::Type::Array;
    for (u64 h : handles) {
        JsonValue o;
        o.type = JsonValue::Type::Object;
        o.obj_val["handle"] = JsonValue(h);
        arr.arr_val.push_back(std::move(o));
    }
    return arr.stringify();
}
} // namespace

// ===========================================================================
// SECTION 2 — JavaField
// ===========================================================================

JavaField::~JavaField() {
    if (bridge_handle_ != 0) {
        auto r = JavaScriptBridge::instance().release_handle(bridge_handle_);
        (void)r;
        bridge_handle_ = 0;
    }
}

Result<JavaReply> JavaField::get(Ptr<JavaInstance> inst) const {
    if (!cls_) return Result<JavaReply>::err(ErrorCode::InvalidArgument, "no class");
    auto& b = JavaScriptBridge::instance();
    if (is_static_)
        return b.read_static(cls_->name(), name_);
    if (!inst) return Result<JavaReply>::err(ErrorCode::InvalidArgument, "no instance");
    u64 id = static_cast<u64>(time_util::now_ms()) & 0xFFFFFF;
    String cmd = "{\"id\":" + std::to_string(id) +
                 ",\"action\":\"cpp_read_field\","
                 "\"handleId\":" + std::to_string(inst->handle()) +
                 ",\"fieldName\":\"" + str::escape_json(name_) + "\"}";
    return b.call(cmd);
}

Result<void> JavaField::set(const JsonValue& v, Ptr<JavaInstance> inst) {
    if (!cls_) return Result<void>::err(ErrorCode::InvalidArgument, "no class");
    auto& b = JavaScriptBridge::instance();
    if (is_static_) {
        auto r = b.write_static(cls_->name(), name_, v.stringify());
        if (!r) return Result<void>::err(r.error_code(), r.error_message());
        return Result<void>::ok();
    }
    if (!inst) return Result<void>::err(ErrorCode::InvalidArgument, "no instance");
    u64 id = static_cast<u64>(time_util::now_ms()) & 0xFFFFFF;
    String cmd = "{\"id\":" + std::to_string(id) +
                 ",\"action\":\"cpp_write_field\","
                 "\"handleId\":" + std::to_string(inst->handle()) +
                 ",\"fieldName\":\"" + str::escape_json(name_) + "\","
                 "\"value\":" + v.stringify() + "}";
    auto r = b.call(cmd);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

// ===========================================================================
// SECTION 3 — JavaMethod
// ===========================================================================

JavaMethod::~JavaMethod() {
    if (hook_handle_id_ != 0) {
        // silent unhook — error is not actionable at destruction time
        try {
            JavaHookManager::instance().unhook(hook_handle_id_);
        } catch (...) {}
        hook_handle_id_ = 0;
    }
}

Result<JavaReply> JavaMethod::call(Ptr<JavaInstance> inst,
                                    const std::vector<JsonValue>& args)
{
    auto& b = JavaScriptBridge::instance();
    return b.call_method(inst ? inst->handle() : 0, handle_,
                          build_args_json(args));
}

Result<JavaReply> JavaMethod::call_static(const std::vector<JsonValue>& args) {
    auto& b = JavaScriptBridge::instance();
    return b.call_method(0, handle_, build_args_json(args));
}

Result<void> JavaMethod::hook(std::function<void(const std::vector<u64>&, u64)> cb) {
    // كل الـ hooks تمر عبر JavaHookManager — لا سجل ثانٍ محلي.
    if (!cls_) {
        return Result<void>::err(ErrorCode::InvalidArgument, "no class for hook");
    }
    auto r = JavaHookManager::instance().hook(
        cls_->name(), name_, sig_, std::move(cb));
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    hook_handle_id_ = r.value();
    return Result<void>::ok();
}

Result<void> JavaMethod::unhook() {
    if (hook_handle_id_ != 0) {
        auto r = JavaHookManager::instance().unhook(hook_handle_id_);
        hook_handle_id_ = 0;
        if (!r) return Result<void>::err(r.error_code(), r.error_message());
    }
    return Result<void>::ok();
}

Result<void> JavaMethod::deoptimize() {
    auto r = JavaScriptBridge::instance().deopt_method(handle_);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

// ===========================================================================
// SECTION 4 — JavaArray
// ===========================================================================

Result<u64> JavaArray::length() const {
    auto r = JavaScriptBridge::instance().array_length(handle_);
    if (!r) return Result<u64>::err(r.error_code(), r.error_message());
    i64 v = 0;
    str::parse_i64(r.value().result, v);
    return Result<u64>::ok(static_cast<u64>(v));
}

Result<JavaReply> JavaArray::get(usize index) const {
    return JavaScriptBridge::instance().array_get(handle_, index);
}

Result<void> JavaArray::set(usize index, const JsonValue& value) {
    auto& b = JavaScriptBridge::instance();
    u64 id = time_util::now_ms();
    String cmd = "{\"id\":" + std::to_string(id) +
                 ",\"action\":\"cpp_array_set\","
                 "\"handle\":" + std::to_string(handle_) + ","
                 "\"index\":" + std::to_string(index) + ","
                 "\"value\":" + value.stringify() + "}";
    auto r = b.call(cmd);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

Result<Ptr<JavaArray>> JavaArray::create(const String& et,
                                          const std::vector<JsonValue>& elems)
{
    JsonValue arr;
    arr.type = JsonValue::Type::Array;
    for (auto& e : elems) arr.arr_val.push_back(e);
    auto r = JavaScriptBridge::instance().array_of(et, arr.stringify());
    if (!r) return Result<Ptr<JavaArray>>::err(r.error_code(), r.error_message());
    if (r.value().kind != "handle")
        return Result<Ptr<JavaArray>>::err(ErrorCode::TypeError, "not a handle");
    return Result<Ptr<JavaArray>>::ok(
        std::make_shared<JavaArray>(r.value().handle, et));
}

Result<Ptr<JavaArray>> JavaArray::from_handles(const String& et,
                                                const std::vector<u64>& handles)
{
    auto r = JavaScriptBridge::instance().array_of(et, handles_to_json(handles));
    if (!r) return Result<Ptr<JavaArray>>::err(r.error_code(), r.error_message());
    if (r.value().kind != "handle")
        return Result<Ptr<JavaArray>>::err(ErrorCode::TypeError, "not a handle");
    return Result<Ptr<JavaArray>>::ok(
        std::make_shared<JavaArray>(r.value().handle, et));
}

// ===========================================================================
// SECTION 5 — JavaInstance
// ===========================================================================

Result<Ptr<JavaMethod>> JavaInstance::method(const String& n, const String& sig) {
    if (!cls_) return Result<Ptr<JavaMethod>>::err(ErrorCode::InvalidArgument, "no class");
    return cls_->method(n, sig);
}

Result<Ptr<JavaField>> JavaInstance::field(const String& n) {
    if (!cls_) return Result<Ptr<JavaField>>::err(ErrorCode::InvalidArgument, "no class");
    return cls_->field(n);
}

Result<JavaReply> JavaInstance::call(const String& m, const String& sig,
                                       const std::vector<JsonValue>& args)
{
    auto mm = method(m, sig);
    if (!mm) return Result<JavaReply>::err(mm.error_code(), mm.error_message());
    return mm.value()->call(shared_from_this(), args);
}

Result<JavaReply> JavaInstance::get(const String& f) {
    auto ff = field(f);
    if (!ff) return Result<JavaReply>::err(ff.error_code(), ff.error_message());
    return ff.value()->get(shared_from_this());
}

Result<void> JavaInstance::set(const String& f, const JsonValue& v) {
    auto ff = field(f);
    if (!ff) return Result<void>::err(ff.error_code(), ff.error_message());
    return ff.value()->set(v, shared_from_this());
}

Result<JsonValue> JavaInstance::snapshot() {
    auto r = JavaScriptBridge::instance().snapshot_fields(handle_);
    if (!r) return Result<JsonValue>::err(r.error_code(), r.error_message());
    auto parsed = JsonValue::parse(r.value().result);
    if (!parsed) return Result<JsonValue>::err(ErrorCode::JsonParseError, "snapshot");
    return Result<JsonValue>::ok(std::move(parsed.value()));
}

Result<u64> JavaInstance::retain() {
    auto r = JavaScriptBridge::instance().retain(handle_);
    if (!r) return Result<u64>::err(r.error_code(), r.error_message());
    return Result<u64>::ok(handle_);
}

// ===========================================================================
// SECTION 6 — JavaClass
// ===========================================================================

Result<Ptr<JavaMethod>> JavaClass::method(const String& n, const String& sig) {
    auto r = JavaScriptBridge::instance().get_method(handle_, n, sig);
    if (!r) return Result<Ptr<JavaMethod>>::err(r.error_code(), r.error_message());
    if (r.value().kind != "handle")
        return Result<Ptr<JavaMethod>>::err(ErrorCode::MethodNotFound, n);
    return Result<Ptr<JavaMethod>>::ok(std::make_shared<JavaMethod>(
        shared_from_this(), r.value().handle, n, sig, false));
}

Result<Ptr<JavaField>> JavaClass::field(const String& n) {
    if (n.empty())
        return Result<Ptr<JavaField>>::err(ErrorCode::InvalidArgument, "empty");

    // Ask the JS bridge for the field. The cpp_get_field command replies with
    // a JSON string: {"static":bool, "handle":N}.
    bool is_static = false;
    u64 bridge_handle = 0;
    {
        auto r = JavaScriptBridge::instance().get_field_by_name(name_, n);
        if (r && r.value().ok && !r.value().result.empty()) {
            auto parsed = JsonValue::parse(r.value().result);
            if (parsed) {
                auto& p = parsed.value();
                if (auto* s = p.get("static")) is_static = s->as_bool(false);
                if (auto* h = p.get("handle"))
                    bridge_handle = static_cast<u64>(h->as_i64(0));
            }
        }
    }

    if (bridge_handle == 0) {
        return Result<Ptr<JavaField>>::err(ErrorCode::FieldNotFound, n);
    }

    return Result<Ptr<JavaField>>::ok(
        std::make_shared<JavaField>(shared_from_this(), n, is_static,
                                     bridge_handle));
}

Result<Ptr<JavaInstance>> JavaClass::create(const std::vector<JsonValue>& args) {
    auto r = JavaScriptBridge::instance().new_instance(handle_, build_args_json(args));
    if (!r) return Result<Ptr<JavaInstance>>::err(r.error_code(), r.error_message());
    if (r.value().kind != "handle")
        return Result<Ptr<JavaInstance>>::err(ErrorCode::TypeError, "not a handle");
    return Result<Ptr<JavaInstance>>::ok(
        std::make_shared<JavaInstance>(shared_from_this(), r.value().handle));
}

Result<JavaReply> JavaClass::call_static(const String& m, const String& sig,
                                           const std::vector<JsonValue>& args)
{
    auto mm = method(m, sig);
    if (!mm) return Result<JavaReply>::err(mm.error_code(), mm.error_message());
    return mm.value()->call_static(args);
}

Result<JavaReply> JavaClass::read_static(const String& f) {
    return JavaScriptBridge::instance().read_static(name_, f);
}

Result<void> JavaClass::write_static(const String& f, const JsonValue& v) {
    auto r = JavaScriptBridge::instance().write_static(name_, f, v.stringify());
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

Result<std::vector<String>> JavaClass::own_members() {
    auto r = JavaScriptBridge::instance().list_own(name_);
    if (!r) return Result<std::vector<String>>::err(r.error_code(), r.error_message());
    return Result<std::vector<String>>::ok(
        detail::json::strings_of_array(r.value().result));
}

Result<std::vector<String>> JavaClass::overloads(const String& m) {
    auto r = JavaScriptBridge::instance().list_overloads(name_, m);
    if (!r) return Result<std::vector<String>>::err(r.error_code(), r.error_message());
    return Result<std::vector<String>>::ok(
        detail::json::strings_of_array(r.value().result));
}

Result<std::vector<String>> JavaClass::static_fields() {
    auto r = JavaScriptBridge::instance().list_static_fields(name_);
    if (!r) return Result<std::vector<String>>::err(r.error_code(), r.error_message());
    return Result<std::vector<String>>::ok(
        detail::json::strings_of_array(r.value().result));
}

Result<JsonValue> JavaClass::inspect() {
    auto r = JavaScriptBridge::instance().probe_class(name_);
    if (!r) return Result<JsonValue>::err(r.error_code(), r.error_message());
    auto parsed = JsonValue::parse(r.value().result);
    if (!parsed) return Result<JsonValue>::err(ErrorCode::JsonParseError, "inspect");
    return Result<JsonValue>::ok(std::move(parsed.value()));
}

Result<void> JavaClass::deoptimize_all_methods() {
    auto r = JavaScriptBridge::instance().deopt_class(name_);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

Result<usize> JavaClass::choose(std::function<int(Ptr<JavaInstance>)> cb) {
    auto r = JavaScriptBridge::instance().choose(name_);
    if (!r) return Result<usize>::err(r.error_code(), r.error_message());

    auto parsed = JsonValue::parse(r.value().result);
    usize count = 0;
    if (parsed && parsed.value().is_arr()) {
        for (auto& e : parsed.value().arr_val) {
            u64 hid = static_cast<u64>(e.as_i64(0));
            auto inst = std::make_shared<JavaInstance>(shared_from_this(), hid);
            ++count;
            if (cb) {
                int rc = cb(inst);
                if (rc != 0) break;
            }
        }
    }
    return Result<usize>::ok(count);
}

// ===========================================================================
// SECTION 6 — JavaHook
// ===========================================================================

JavaHook::JavaHook() = default;
JavaHook::~JavaHook() {
    if (installed_) uninstall();
}

Result<void> JavaHook::install(const String& cls,
                                const String& m,
                                const String& sig,
                                std::function<void(const std::vector<u64>&, u64)> cb)
{
    class_name_ = cls;
    method_name_ = m;
    signature_ = sig;
    cb_ = std::move(cb);

    // Use the global JavaHookManager which handles all the plumbing.
    auto r = JavaHookManager::instance().hook(cls, m, sig,
        [this](const std::vector<u64>& a, u64 t) {
            if (cb_) {
                try { cb_(a, t); }
                catch (const std::exception& e) {
                    YAM_LOG_ERROR() << "JavaHook cb: " << e.what();
                }
            }
        });
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    method_handle_ = r.value();
    installed_ = true;
    return Result<void>::ok();
}

Result<void> JavaHook::uninstall() {
    if (!installed_ && method_handle_ == 0) {
        return Result<void>::ok();
    }
    Result<void> r = Result<void>::ok();
    if (method_handle_ != 0) {
        r = JavaHookManager::instance().unhook(method_handle_);
        // صفّر الحالة حتى لو فشل الإلغاء — لا نُبقِي handle قديماً.
        method_handle_ = 0;
    }
    installed_ = false;
    cb_id_ = 0;
    cb_ = nullptr;
    return r;
}

// ===========================================================================
// SECTION 7 — namespace java
// ===========================================================================

namespace java {

Result<Ptr<JavaClass>> use(const String& cls) {
    auto r = JavaScriptBridge::instance().use_class(cls);
    if (!r) return Result<Ptr<JavaClass>>::err(r.error_code(), r.error_message());
    if (r.value().kind != "handle")
        return Result<Ptr<JavaClass>>::err(ErrorCode::ClassNotFound, cls);
    return Result<Ptr<JavaClass>>::ok(
        std::make_shared<JavaClass>(r.value().handle, cls));
}

Result<Ptr<JavaInstance>> cast(Ptr<JavaInstance> inst, const String& cls) {
    if (!inst) return Result<Ptr<JavaInstance>>::err(ErrorCode::InvalidArgument, "no inst");
    auto c = use(cls);
    if (!c) return Result<Ptr<JavaInstance>>::err(c.error_code(), c.error_message());
    auto r = JavaScriptBridge::instance().cast(c.value()->handle(), inst->handle());
    if (!r) return Result<Ptr<JavaInstance>>::err(r.error_code(), r.error_message());
    if (r.value().kind != "handle")
        return Result<Ptr<JavaInstance>>::err(ErrorCode::TypeError, "cast failed");
    return Result<Ptr<JavaInstance>>::ok(
        std::make_shared<JavaInstance>(c.value(), r.value().handle));
}

Result<Ptr<JavaArray>> array(const String& et, const std::vector<JsonValue>& elems) {
    return JavaArray::create(et, elems);
}

Result<u64> retain(Ptr<JavaInstance> inst) {
    if (!inst) return Result<u64>::err(ErrorCode::InvalidArgument, "no inst");
    return inst->retain();
}

Result<std::vector<Ptr<JavaInstance>>> choose_all(const String& cls) {
    auto c = use(cls);
    if (!c) return Result<std::vector<Ptr<JavaInstance>>>::err(c.error_code(),
                                                                c.error_message());
    std::vector<Ptr<JavaInstance>> out;
    auto r = c.value()->choose([&out](Ptr<JavaInstance> i) -> int {
        out.push_back(i);
        return 0;
    });
    if (!r) return Result<std::vector<Ptr<JavaInstance>>>::err(r.error_code(),
                                                                r.error_message());
    return Result<std::vector<Ptr<JavaInstance>>>::ok(std::move(out));
}

Result<std::vector<String>> enumerate_classes() {
    auto r = JavaScriptBridge::instance().enumerate_classes();
    if (!r) return Result<std::vector<String>>::err(r.error_code(), r.error_message());
    return Result<std::vector<String>>::ok(
        detail::json::strings_of_array(r.value().result));
}

Result<std::vector<String>> enumerate_loaders() {
    auto r = JavaScriptBridge::instance().enumerate_loaders();
    if (!r) return Result<std::vector<String>>::err(r.error_code(), r.error_message());
    return Result<std::vector<String>>::ok(
        detail::json::strings_of_array(r.value().result));
}

Result<void> deoptimize_everything() {
    auto r = JavaScriptBridge::instance().deopt_everything();
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> deoptimize_boot_image() {
    auto r = JavaScriptBridge::instance().deopt_boot_image();
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}
Result<void> deoptimize_class(const String& c) {
    auto r = JavaScriptBridge::instance().deopt_class(c);
    if (!r) return Result<void>::err(r.error_code(), r.error_message());
    return Result<void>::ok();
}

Result<void> synchronized(Ptr<JavaInstance> inst, std::function<void()> fn) {
    if (!fn) return Result<void>::err(ErrorCode::InvalidArgument, "null fn");
    // We can't literally lock Java's monitor without a C API.
    // But we can use a global C++ mutex to serialize.
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    try { fn(); }
    catch (const std::exception& e) {
        return Result<void>::err(ErrorCode::InternalError, e.what());
    }
    return Result<void>::ok();
}

} // namespace java

} // namespace yam
