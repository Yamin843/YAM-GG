#include "ClassBrowser.h"
#include "../../Bridge/YamBridge.h"
#include "../../UI/Widgets/Notification.h"

#include "imgui.h"
#include "../../all_libs/YAM-G/wrapper/include/yam.hpp"

#include <android/log.h>
#include <cstring>
#include <cstdio>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

ClassBrowser& ClassBrowser::instance() {
    static ClassBrowser inst;
    return inst;
}

ClassBrowser::ClassBrowser() { registerEvents(); }
ClassBrowser::~ClassBrowser() = default;

void ClassBrowser::registerEvents() {
    if (eventsRegistered_) return;
    eventsRegistered_ = true;

    yam::events::on("classes_result", [this](const yam::Event& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        classes_.clear();
        auto* arr = ev.data.get("items");
        if (arr && arr->is_arr()) {
            for (auto& e : arr->arr_val) {
                std::string n = e.as_str();
                if (!n.empty()) classes_.push_back(n);
            }
        }
        loading_ = false;
        LOGI("ClassBrowser: %zu classes", classes_.size());
    });

    yam::events::on("methods_result", [this](const yam::Event& ev) {
        std::string cls = ev.get_str("className");
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<MethodInfo> list;
        auto* arr = ev.data.get("items");
        if (arr && arr->is_arr()) {
            for (auto& item : arr->arr_val) {
                MethodInfo mi;
                if (auto* n = item.get("name")) mi.name = n->as_str();
                if (auto* r = item.get("ret"))  mi.ret  = r->as_str();
                if (auto* a = item.get("args")) {
                    if (a->is_arr()) {
                        for (auto& e : a->arr_val) {
                            ParamInfo pi;
                            if (e.is_obj()) {
                                if (auto* tn = e.get("typeName")) pi.typeName = tn->as_str();
                                if (auto* jn = e.get("jniType"))  pi.jniType  = jn->as_str();
                            } else {
                                pi.typeName = e.as_str();
                            }
                            mi.args.push_back(std::move(pi));
                        }
                    }
                }
                list.push_back(std::move(mi));
            }
        }
        methodsCache_[cls] = std::move(list);
        selectedClass_ = cls;
        selectedMethodIdx_ = -1;
        loading_ = false;
        LOGI("ClassBrowser: %s -> %zu methods", cls.c_str(),
             methodsCache_[cls].size());
    });

    yam::events::on("classes_error", [this](const yam::Event&) {
        std::lock_guard<std::mutex> lk(mu_);
        loading_ = false;
    });
    yam::events::on("methods_error", [this](const yam::Event&) {
        std::lock_guard<std::mutex> lk(mu_);
        loading_ = false;
    });
}

void ClassBrowser::sendJS(const std::string& js) {
    YamBridge::instance().postEval(js);
}

void ClassBrowser::triggerLoadClasses() {
    if (filter_.empty()) filter_ = "com.";
    loading_ = true;
    classes_.clear();

    std::string f = yam::JsonValue(filter_).stringify();
    std::string js =
        "(function(){try{"
        "var all=Java.enumerateLoadedClassesSync();"
        "var f=" + f + ";"
        "var out=[];"
        "for(var i=0;i<all.length&&out.length<500;i++){"
        "if(all[i].indexOf(f)>=0)out.push(all[i]);}"
        "send({type:'classes_result',items:out});"
        "}catch(e){send({type:'classes_error',message:''+e});}})();";
    sendJS(js);
}

void ClassBrowser::triggerLoadMethods(const std::string& cls) {
    loading_ = true;
    std::string cn = yam::JsonValue(cls).stringify();
    std::string js =
        "(function(){try{"
        "var c=Java.use(" + cn + ");"
        "var ms=c.class.getDeclaredMethods();"
        "var out=[];"
        "for(var i=0;i<ms.length;i++){"
        "try{"
        "var m=ms[i];"
        "m.setAccessible(true);"
        "var pts=m.getParameterTypes();"
        "var args=[];"
        "for(var j=0;j<pts.length;j++){"
        "var tn=String(pts[j].getName());"
        "args.push({typeName:tn,jniType:tn});"
        "}"
        "out.push({name:String(m.getName()),"
        "ret:String(m.getReturnType().getName()),"
        "args:args});"
        "}catch(e){}"
        "}"
        "send({type:'methods_result',className:" + cn + ",items:out});"
        "}catch(e){send({type:'methods_error',message:''+e});}})();";
    sendJS(js);
}

void ClassBrowser::triggerTrace(const std::string& cls, const MethodInfo& m) {
    // Send a run_user_script to hook the method
    std::string cn = yam::JsonValue(cls).stringify();
    std::string mn = yam::JsonValue(m.name).stringify();
    std::string code =
        "try{var C=Java.use(" + cn + ");"
        "var MM=C[" + mn + "];"
        "if(MM&&MM.overloads){"
        "for(var i=0;i<MM.overloads.length;i++){"
        "(function(ov){"
        "var orig=ov.implementation;"
        "ov.implementation=function(){"
        "console.log('[trace] " + cls + "." + m.name + " called');"
        "return ov.apply(this,arguments);"
        "};})(MM.overloads[i]);"
        "}}"
        "send({type:'trace_ok',className:" + cn + ",method:" + mn + "});"
        "}catch(e){send({type:'trace_err',message:''+e});}";
    std::string js =
        "(function(){try{" + code + "}catch(e){send({type:'trace_err',message:''+e});}})();";
    sendJS(js);
    Notification::instance().push("Tracing: " + m.name);
}

void ClassBrowser::triggerCall(const std::string& cls, const MethodInfo& m) {
    std::string cn = yam::JsonValue(cls).stringify();
    std::string mn = yam::JsonValue(m.name).stringify();
    // Try to call with null args
    std::string code =
        "try{var C=Java.use(" + cn + ");"
        "var MM=C[" + mn + "];"
        "if(MM&&MM.overloads&&MM.overloads.length>0){"
        "var ov=MM.overloads[0];"
        "var n=ov.argumentTypes.length;"
        "var a=[];for(var k=0;k<n;k++)a.push(null);"
        "var ret=ov.apply(null,a);"
        "send({type:'call_ok',result:''+ret});"
        "}else{send({type:'call_err',message:'no overload'});}"
        "}catch(e){send({type:'call_err',message:''+e});}";
    std::string js =
        "(function(){try{" + code + "}catch(e){send({type:'call_err',message:''+e});}})();";
    sendJS(js);
}

void ClassBrowser::draw() {
    drawTopBar();
    ImGui::Separator();

    float avail = ImGui::GetContentRegionAvail().x;
    float colW = avail * 0.4f;

    ImGui::BeginChild("##ClassesPane", ImVec2(colW, -160), true);
    drawClassesTree();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##MethodsPane", ImVec2(0, -160), true);
    drawMethodsList();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("##MethodDetail", ImVec2(0, 0), true);
    drawMethodDetail();
    ImGui::EndChild();
}

void ClassBrowser::drawTopBar() {
    ImGui::Spacing();
    ImGui::SetNextItemWidth(400);
    if (ImGui::InputTextWithHint("##filter", "package (e.g. com.example)",
                                 filterBuf_, sizeof(filterBuf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        filter_ = filterBuf_;
        triggerLoadClasses();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(90, 0))) {
        filter_ = filterBuf_;
        if (filter_.empty()) filter_ = "com.";
        triggerLoadClasses();
    }
    ImGui::SameLine();
    if (ImGui::Button("Classes only", ImVec2(130, 0))) {
        filter_ = "android.";
        std::strncpy(filterBuf_, filter_.c_str(), sizeof(filterBuf_) - 1);
        triggerLoadClasses();
    }
    ImGui::SameLine();
    if (loading_) ImGui::TextDisabled("loading...");
}

void ClassBrowser::drawClassesTree() {
    std::lock_guard<std::mutex> lk(mu_);
    ImGui::Text("Classes (%zu)", classes_.size());
    ImGui::Separator();

    // Simple flat tree with package grouping
    std::string lastPkg;
    for (auto& c : classes_) {
        size_t dot = c.find_last_of('.');
        std::string pkg = (dot == std::string::npos) ? "" : c.substr(0, dot);
        std::string cls = (dot == std::string::npos) ? c : c.substr(dot + 1);

        if (pkg != lastPkg) {
            if (!lastPkg.empty()) ImGui::TreePop();
            ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_DefaultOpen
                | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (ImGui::TreeNodeEx(pkg.c_str(), fl)) {
                lastPkg = pkg;
            } else {
                lastPkg = pkg;
                continue;
            }
        }

        bool sel = (selectedClass_ == c);
        if (ImGui::Selectable(cls.c_str(), sel)) {
            if (selectedClass_ != c) {
                triggerLoadMethods(c);
            }
        }
    }
    if (!lastPkg.empty()) ImGui::TreePop();
}

void ClassBrowser::drawMethodsList() {
    std::lock_guard<std::mutex> lk(mu_);
    if (selectedClass_.empty()) {
        ImGui::TextDisabled("Select a class");
        return;
    }
    auto it = methodsCache_.find(selectedClass_);
    if (it == methodsCache_.end()) {
        ImGui::TextDisabled("Loading methods...");
        return;
    }
    auto& methods = it->second;
    ImGui::Text("Methods (%zu)", methods.size());
    ImGui::Separator();
    for (size_t i = 0; i < methods.size(); ++i) {
        auto& m = methods[i];
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s %s(%zu)",
                      m.ret.c_str(), m.name.c_str(), m.args.size());
        bool sel = ((int)i == selectedMethodIdx_);
        ImGui::PushID((int)i);
        if (ImGui::Selectable(buf, sel)) selectedMethodIdx_ = (int)i;
        ImGui::PopID();
    }
}

void ClassBrowser::drawMethodDetail() {
    std::lock_guard<std::mutex> lk(mu_);
    if (selectedClass_.empty() || selectedMethodIdx_ < 0) {
        ImGui::TextDisabled("Select a method to view its parameters");
        return;
    }
    auto it = methodsCache_.find(selectedClass_);
    if (it == methodsCache_.end()) return;
    auto& methods = it->second;
    if (selectedMethodIdx_ >= (int)methods.size()) return;
    auto& m = methods[selectedMethodIdx_];

    ImGui::Text("%s %s", m.ret.c_str(), m.name.c_str());
    ImGui::Separator();

    if (m.args.empty()) {
        ImGui::TextDisabled("(no parameters)");
    } else {
        if (ImGui::BeginTable("##params", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("JNI");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < m.args.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", m.args[i].typeName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", m.args[i].jniType.c_str());
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("trace", ImVec2(120, 40))) {
        triggerTrace(selectedClass_, m);
    }
    ImGui::SameLine();
    if (ImGui::Button("call", ImVec2(120, 40))) {
        triggerCall(selectedClass_, m);
    }
    ImGui::SameLine();
    if (ImGui::Button("copy sig", ImVec2(120, 40))) {
        // Build JNI signature
        std::string sig = "(";
        for (auto& a : m.args) sig += "?";  // simplified
        sig += ")";
        ImGui::SetClipboardText(sig.c_str());
    }
}

} // namespace yamgg
