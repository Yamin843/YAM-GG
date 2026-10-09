#ifndef YAM_JAVA_MODEL_HPP
#define YAM_JAVA_MODEL_HPP

// ===========================================================================
// yam_java_model.hpp — C++ object model for Java objects via the bridge
// ===========================================================================

#include "yam.hpp"

namespace yam {

class JavaClass;
class JavaMethod;
class JavaField;
class JavaInstance;
class JavaArray;
class JavaHook;

// ---------------------------------------------------------------------------
// JavaField — direct field access
// ---------------------------------------------------------------------------

class JavaField {
public:
    JavaField() = default;
    JavaField(Ptr<JavaClass> cls, const String& name, bool is_static)
        : cls_(std::move(cls)), name_(name), is_static_(is_static) {}

    YAM_NODISCARD const String& name() const noexcept { return name_; }
    YAM_NODISCARD bool is_static() const noexcept { return is_static_; }
    YAM_NODISCARD Ptr<JavaClass> holder() const noexcept { return cls_; }

    Result<JavaReply> get(Ptr<JavaInstance> inst = nullptr) const;
    Result<void> set(const JsonValue& v, Ptr<JavaInstance> inst = nullptr);

private:
    Ptr<JavaClass> cls_;
    String name_;
    bool is_static_{false};
};

// ---------------------------------------------------------------------------
// JavaMethod
// ---------------------------------------------------------------------------

class JavaMethod {
public:
    JavaMethod() = default;
    JavaMethod(Ptr<JavaClass> cls, u64 handle,
               const String& name, const String& sig, bool is_static)
        : cls_(std::move(cls)), handle_(handle),
          name_(name), sig_(sig), is_static_(is_static) {}

    YAM_NODISCARD u64 handle() const noexcept { return handle_; }
    YAM_NODISCARD const String& name() const noexcept { return name_; }
    YAM_NODISCARD const String& signature() const noexcept { return sig_; }
    YAM_NODISCARD bool is_static() const noexcept { return is_static_; }

    Result<JavaReply> call(Ptr<JavaInstance> inst,
                            const std::vector<JsonValue>& args);
    Result<JavaReply> call_static(const std::vector<JsonValue>& args);

    Result<void> hook(std::function<void(const std::vector<u64>&, u64)> cb);
    Result<void> unhook();
    Result<void> deoptimize();

private:
    Ptr<JavaClass> cls_;
    u64    handle_{0};
    String name_;
    String sig_;
    bool   is_static_{false};
    i64    hook_cb_id_{0};
};

// ---------------------------------------------------------------------------
// JavaArray
// ---------------------------------------------------------------------------

class JavaArray {
public:
    JavaArray() = default;
    JavaArray(u64 h, const String& et) : handle_(h), element_type_(et) {}

    YAM_NODISCARD u64 handle() const noexcept { return handle_; }
    YAM_NODISCARD const String& element_type() const noexcept { return element_type_; }

    Result<u64> length() const;
    Result<JavaReply> get(usize index) const;
    Result<void> set(usize index, const JsonValue& value);

    static Result<Ptr<JavaArray>> create(const String& element_type,
                                          const std::vector<JsonValue>& elems);
    static Result<Ptr<JavaArray>> from_handles(const String& element_type,
                                                const std::vector<u64>& handles);

private:
    u64    handle_{0};
    String element_type_;
};

// ---------------------------------------------------------------------------
// JavaInstance
// ---------------------------------------------------------------------------

class JavaInstance : public std::enable_shared_from_this<JavaInstance> {
public:
    JavaInstance() = default;
    JavaInstance(Ptr<JavaClass> cls, u64 h)
        : cls_(std::move(cls)), handle_(h) {}

    YAM_NODISCARD u64 handle() const noexcept { return handle_; }
    YAM_NODISCARD Ptr<JavaClass> klass() const noexcept { return cls_; }

    Result<Ptr<JavaMethod>> method(const String& name, const String& sig = "");
    Result<Ptr<JavaField>>  field(const String& name);

    Result<JavaReply> call(const String& method, const String& sig,
                            const std::vector<JsonValue>& args = {});
    Result<JavaReply> get(const String& field);
    Result<void> set(const String& field, const JsonValue& value);

    Result<JsonValue> snapshot();
    Result<u64> retain();

private:
    Ptr<JavaClass> cls_;
    u64 handle_{0};
};

// ---------------------------------------------------------------------------
// JavaClass
// ---------------------------------------------------------------------------

class JavaClass : public std::enable_shared_from_this<JavaClass> {
public:
    JavaClass() = default;
    JavaClass(u64 h, const String& name) : handle_(h), name_(name) {}

    YAM_NODISCARD u64 handle() const noexcept { return handle_; }
    YAM_NODISCARD const String& name() const noexcept { return name_; }

    Result<Ptr<JavaMethod>> method(const String& name, const String& sig = "");
    Result<Ptr<JavaField>>  field(const String& name);

    Result<Ptr<JavaInstance>> create(const std::vector<JsonValue>& args = {});

    Result<JavaReply> call_static(const String& method, const String& sig,
                                    const std::vector<JsonValue>& args = {});
    Result<JavaReply> read_static(const String& field);
    Result<void> write_static(const String& field, const JsonValue& value);

    Result<std::vector<String>> own_members();
    Result<std::vector<String>> overloads(const String& method);
    Result<std::vector<String>> static_fields();

    Result<JsonValue> inspect();

    Result<void> deoptimize_all_methods();
    Result<usize> choose(std::function<int(Ptr<JavaInstance>)> cb);

private:
    u64 handle_{0};
    String name_;
};

// ---------------------------------------------------------------------------
// JavaHook — high-level method hook
// ---------------------------------------------------------------------------

class JavaHook {
public:
    JavaHook();
    ~JavaHook();
    YAM_NONCOPYABLE(JavaHook);

    Result<void> install(const String& class_name,
                          const String& method_name,
                          const String& signature,
                          std::function<void(const std::vector<u64>& args,
                                              u64 this_handle)> cb);
    Result<void> uninstall();
    YAM_NODISCARD bool installed() const noexcept { return installed_; }

private:
    String class_name_;
    String method_name_;
    String signature_;
    u64    method_handle_{0};
    i64    cb_id_{0};
    bool   installed_{false};
    std::function<void(const std::vector<u64>&, u64)> cb_;
};

// ---------------------------------------------------------------------------
// namespace java — top-level helpers
// ---------------------------------------------------------------------------

namespace java {

Result<Ptr<JavaClass>> use(const String& class_name);
Result<Ptr<JavaInstance>> cast(Ptr<JavaInstance> inst, const String& class_name);
Result<Ptr<JavaArray>> array(const String& element_type,
                              const std::vector<JsonValue>& elems);
Result<u64> retain(Ptr<JavaInstance> inst);
Result<std::vector<Ptr<JavaInstance>>> choose_all(const String& class_name);
Result<std::vector<String>> enumerate_classes();
Result<std::vector<String>> enumerate_loaders();
Result<void> deoptimize_everything();
Result<void> deoptimize_boot_image();
Result<void> deoptimize_class(const String& cls);
Result<void> synchronized(Ptr<JavaInstance> inst, std::function<void()> fn);

} // namespace java

} // namespace yam

#endif // YAM_JAVA_MODEL_HPP
