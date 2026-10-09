#include "ClassBrowser.h"
#include "../../Bridge/YamBridge.h"
#include "../Widgets/Notification.h"

#include "imgui.h"
#include "../../all_libs/YAM-G/wrapper/include/yam.hpp"

#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

ClassBrowser& ClassBrowser::instance() { static ClassBrowser i; return i; }
ClassBrowser::ClassBrowser() { registerEvents(); }
ClassBrowser::~ClassBrowser() = default;

void ClassBrowser::sendJS(const std::string& js) { YamBridge::instance().postEval(js); }

void ClassBrowser::registerEvents() {
    if (eventsRegistered_) return;
    eventsRegistered_ = true;

    yam::events::on("classes_result", [this](const yam::Event& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        classes_.clear();
        auto* arr = ev.data.get("items");
        if (arr && arr->is_arr())
            for (auto& e : arr->arr_val) classes_.push_back(e.as_str());
        std::sort(classes_.begin(), classes_.end());
        loading_ = false;
        LOGI("ClassBrowser: %zu classes", classes_.size());
    });
    yam::events::on("methods_result", [this](const yam::Event& ev) {
        std::string cls = ev.get_str("className");
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<MethodInfo> list;
        auto* arr = ev.data.get("items");
        if (arr && arr->is_arr()) {
            for (auto& it : arr->arr_val) {
                MethodInfo mi;
                if (auto* n = it.get("name")) mi.name = n->as_str();
                if (auto* r = it.get("ret"))  mi.ret  = r->as_str();
                if (auto* a = it.get("args")) if (a->is_arr()) {
                    for (auto& e : a->arr_val) {
                        ParamInfo pi;
                        if (auto* tn = e.get("typeName")) pi.typeName = tn->as_str();
                        if (auto* kd = e.get("kind"))     pi.kind     = kd->as_str();
                        if (auto* ev2 = e.get("enumValues")) if (ev2->is_arr())
                            for (auto& x : ev2->arr_val) pi.enumValues.push_back(x.as_str());
                        pi.scalar[0] = 0;
                        mi.args.push_back(std::move(pi));
                    }
                }
                list.push_back(std::move(mi));
            }
        }
        methods_[cls] = std::move(list);
        selectedClass_ = cls;
        loading_ = false;
    });
    auto err = [this](const yam::Event&) { std::lock_guard<std::mutex> lk(mu_); loading_ = false; };
    yam::events::on("classes_error", err);
    yam::events::on("methods_error", err);
}

void ClassBrowser::triggerLoadClasses() {
    if (filter_.empty()) filter_ = "com.";
    loading_ = true;
    { std::lock_guard<std::mutex> lk(mu_); classes_.clear(); }
    std::string f = yam::JsonValue(filter_).stringify();
    std::string js =
        "(function(){try{"
        "var all=Java.enumerateLoadedClassesSync();"
        "var f=" + f + ";"
        "var out=[];"
        "for(var i=0;i<all.length&&out.length<1500;i++){"
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
        "for(var i=0;i<ms.length;i++){try{var m=ms[i];m.setAccessible(true);"
        "var pts=m.getParameterTypes();var args=[];"
        "for(var j=0;j<pts.length;j++){var tn=String(pts[j].getName());"
        "var kd='object',ev=[];"
        "if(tn==='int')kd='int';else if(tn==='long')kd='long';"
        "else if(tn==='short')kd='short';else if(tn==='byte')kd='byte';"
        "else if(tn==='float')kd='float';else if(tn==='double')kd='double';"
        "else if(tn==='boolean')kd='boolean';else if(tn==='char')kd='char';"
        "else if(tn==='java.lang.String')kd='string';"
        "else if(tn.charAt(0)==='[')kd='array';"
        "else if(tn.indexOf('Map')>=0)kd='map';"
        "else if(tn.indexOf('List')>=0||tn.indexOf('Set')>=0)kd='array';"
        "else{try{var pc=Java.use(tn);if(pc.class.isEnum()){kd='enum';"
        "var vals=pc.values();for(var e2=0;e2<vals.length;e2++)ev.push(String(vals[e2].name()));}}catch(ex){}}"
        "args.push({typeName:tn,kind:kd,enumValues:ev});}"
        "var mods=m.getModifiers();var isStatic=(mods&8)!==0;"
        "out.push({name:String(m.getName()),ret:String(m.getReturnType().getName()),isStatic:isStatic,args:args});"
        "}catch(e){}}"
        "send({type:'methods_result',className:" + cn + ",items:out});"
        "}catch(e){send({type:'methods_error',message:''+e});}})();";
    sendJS(js);
}

void ClassBrowser::draw() {
    // ─── Advanced search ───
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::SetNextItemWidth(500);
    if (ImGui::InputTextWithHint("##search", "", searchBuf_, sizeof(searchBuf_),
        ImGuiInputTextFlags_EnterReturnsTrue)) {
        filter_ = searchBuf_;
        triggerLoadClasses();
    }
    ImGui::PopTextWrapPos();
    ImGui::SameLine();
    if (ImGui::Button("Find", ImVec2(80, 0))) {
        filter_ = searchBuf_;
        if (filter_.empty()) filter_ = "com.";
        triggerLoadClasses();
    }
    ImGui::SameLine();
    if (loading_) ImGui::TextDisabled("...");
    ImGui::SameLine();
    ImGui::Text("%zu", classes_.size());
    ImGui::Separator();

    ImGui::BeginChild("##TreePanel", ImVec2(0, 0), true);
    drawTree();
    ImGui::EndChild();
}

void ClassBrowser::drawTree() {
    std::lock_guard<std::mutex> lk(mu_);
    if (classes_.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextWrapped("Type and press Enter"); ImGui::PopTextWrapPos(); return; }

    std::string lastPkg;
    std::vector<std::string> members;
    auto flush = [&]() {
        if (!members.empty()) { drawPackageGroup(lastPkg, members); members.clear(); }
    };
    for (auto& c : classes_) {
        size_t dot = c.find_last_of('.');
        std::string pkg = (dot == std::string::npos) ? "<default>" : c.substr(0, dot);
        if (pkg != lastPkg) { flush(); lastPkg = pkg; }
        members.push_back(c);
    }
    flush();
}

void ClassBrowser::drawPackageGroup(const std::string& pkg, std::vector<std::string>& members) {
    ImGui::PushTextWrapPos(0.0f);
    bool pkgOpen = ImGui::TreeNodeEx(pkg.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopTextWrapPos();
    if (!pkgOpen) return;
    for (auto& full : members) {
        size_t dot = full.find_last_of('.');
        std::string simple = (dot == std::string::npos) ? full : full.substr(dot + 1);
        ImGuiTreeNodeFlags cf = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (selectedClass_ == full) cf |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushID(full.c_str());
        ImGui::PushTextWrapPos(0.0f);
        bool open = ImGui::TreeNodeEx(simple.c_str(), cf);
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (selectedClass_ != full && methods_.find(full) == methods_.end())
                triggerLoadMethods(full);
        }
        if (open) {
            auto it = methods_.find(full);
            if (it != methods_.end()) {
                for (auto& m : it->second) drawMethodNode(full, m);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void ClassBrowser::drawMethodNode(const std::string& cls, MethodInfo& m) {
    ImGui::PushID(&m);
    char label[512];
    std::snprintf(label, sizeof(label), "%s %s(%zu)%s",
        m.ret.c_str(), m.name.c_str(), m.args.size(),
        m.tracing ? " [T]" : "");
    ImGui::PushTextWrapPos(0.0f);
    bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopTextWrapPos();
    if (open) {
        for (size_t i = 0; i < m.args.size(); ++i) drawParamWidget(cls, m, m.args[i], (int)i);
        ImGui::Separator();
        if (!m.isStatic) drawInstancePicker(cls, m);
        if (ImGui::Button(m.tracing ? "Stop" : "Trace", ImVec2(120, 40)))
            m.tracing ? traceOff(cls, m) : traceOn(cls, m);
        ImGui::SameLine();
        if (ImGui::Button("Call", ImVec2(120, 40))) callMethod(cls, m);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ClassBrowser::drawParamWidget(const std::string&, MethodInfo&, ParamInfo& p, int idx) {
    ImGui::PushID(idx);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", p.typeName.c_str());
    ImGui::PopTextWrapPos();
    const std::string& k = p.kind;
    if (k == "boolean") {
        ImGui::Checkbox("##b", &p.booleanVal);
    } else if (k == "enum") {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##enum", p.scalar[0] ? p.scalar : "")) {
            for (auto& v : p.enumValues) {
                bool sel = (v == p.scalar);
                if (ImGui::Selectable(v.c_str(), sel)) std::strncpy(p.scalar, v.c_str(), sizeof(p.scalar)-1);
            }
            ImGui::EndCombo();
        }
    } else if (k == "map") {
        ImGui::Indent();
        for (size_t i = 0; i < p.kvs.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::SetNextItemWidth(140);
            ImGui::InputText("##k", p.kvs[i].k, sizeof(p.kvs[i].k));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-60);
            ImGui::InputText("##v", p.kvs[i].v, sizeof(p.kvs[i].v));
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { p.kvs.erase(p.kvs.begin()+i); ImGui::PopID(); break; }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+")) p.kvs.push_back({});
        ImGui::Unindent();
    } else if (k == "array") {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##arr", p.scalar, sizeof(p.scalar), ImVec2(-1, 50));
    } else {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##v", p.scalar, sizeof(p.scalar));
    }
    ImGui::PopID();
}

void ClassBrowser::drawInstancePicker(const std::string& cls, MethodInfo&) {
    auto it = instances_.find(cls);
    if (it == instances_.end() || it->second.empty()) {
        if (ImGui::Button("Instances", ImVec2(140, 36))) findInstances(cls);
        return;
    }
    for (size_t i = 0; i < it->second.size(); ++i) {
        ImGui::PushID((int)i);
        char lbl[128]; std::snprintf(lbl, sizeof(lbl), "#%zu %s", i, it->second[i].className.c_str());
        bool sel = ((int)i == selectedInstance_);
        ImGui::PushTextWrapPos(0.0f);
        if (ImGui::Selectable(lbl, sel)) selectedInstance_ = (int)i;
        ImGui::PopTextWrapPos();
        if (sel && ImGui::BeginTable("##f", 2, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("f"); ImGui::TableSetupColumn("v");
            for (auto& f : it->second[i].fields) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("%s", f.first.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", f.second.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

// trace/call/findInstances/buildArgsJSON — same as before
std::string ClassBrowser::buildArgsJSON(const MethodInfo& m) {
    std::string out = "[";
    for (size_t i = 0; i < m.args.size(); ++i) {
        if (i) out += ",";
        const ParamInfo& p = m.args[i];
        std::string k = p.kind;
        if (k=="int"||k=="long"||k=="short"||k=="byte"||k=="float"||k=="double") {
            out += "{\"kind\":\"" + k + "\",\"value\":" + (p.scalar[0]?p.scalar:"0") + "}";
        } else if (k=="boolean") {
            out += std::string("{\"kind\":\"boolean\",\"value\":") + (p.booleanVal?"true":"false") + "}";
        } else if (k=="char") {
            out += "{\"kind\":\"char\",\"value\":" + yam::JsonValue(std::string(p.scalar[0]?p.scalar:"a")).stringify() + "}";
        } else if (k=="string") {
            out += "{\"kind\":\"string\",\"value\":" + yam::JsonValue(std::string(p.scalar)).stringify() + "}";
        } else if (k=="enum") {
            out += "{\"kind\":\"enum\",\"className\":" + yam::JsonValue(p.typeName).stringify() +
                   ",\"enumName\":" + yam::JsonValue(std::string(p.scalar)).stringify() + "}";
        } else if (k=="map") {
            out += "{\"kind\":\"map\",\"className\":" + yam::JsonValue(p.typeName).stringify() + ",\"entries\":[";
            for (size_t j = 0; j < p.kvs.size(); ++j) {
                if (j) out += ",";
                out += "{\"k\":" + yam::JsonValue(std::string(p.kvs[j].k)).stringify() +
                       ",\"v\":" + yam::JsonValue(std::string(p.kvs[j].v)).stringify() + "}";
            }
            out += "]}";
        } else if (k=="array") {
            out += "{\"kind\":\"arrayJson\",\"className\":" + yam::JsonValue(p.typeName).stringify() +
                   ",\"json\":\"" + yam::str::escape_json(std::string(p.scalar)) + "\"}";
        } else {
            out += "{\"kind\":\"expr\",\"value\":" + yam::JsonValue(std::string(p.scalar)).stringify() + "}";
        }
    }
    out += "]";
    return out;
}

void ClassBrowser::traceOn(const std::string& cls, MethodInfo& m) {
    std::string cn = yam::JsonValue(cls).stringify();
    std::string mn = yam::JsonValue(m.name).stringify();
    std::string js = "(function(){try{var C=Java.use(" + cn + ");var MM=C[" + mn + "];"
        "if(!MM||!MM.overloads)throw new Error('no overloads');"
        "for(var i=0;i<MM.overloads.length;i++){(function(ov){var orig=ov.implementation;ov.__ygg_orig=orig;"
        "ov.implementation=function(){var a=Array.prototype.slice.call(arguments);"
        "var s=[];for(var k=0;k<a.length;k++)s.push(describe(a[k]));"
        "send({type:'console',level:'log',line:'[trace] " + cls + "." + m.name + "('+s.join(', ')+')'});"
        "var r=ov.call(this,...a);return r;};})(MM.overloads[i]);}"
        "send({type:'trace_on_ok'});}catch(e){send({type:'trace_err',message:''+e});}})();";
    sendJS(js); m.tracing = true;
}

void ClassBrowser::traceOff(const std::string& cls, MethodInfo& m) {
    std::string cn = yam::JsonValue(cls).stringify();
    std::string mn = yam::JsonValue(m.name).stringify();
    std::string js = "(function(){try{var C=Java.use(" + cn + ");var MM=C[" + mn + "];"
        "if(MM&&MM.overloads)for(var i=0;i<MM.overloads.length;i++){var ov=MM.overloads[i];"
        "if(ov.__ygg_orig!==undefined){ov.implementation=ov.__ygg_orig;delete ov.__ygg_orig;}else{ov.implementation=null;}}"
        "send({type:'trace_off_ok'});}catch(e){}})();";
    sendJS(js); m.tracing = false;
}

void ClassBrowser::callMethod(const std::string& cls, MethodInfo& m) {
    std::string cn = yam::JsonValue(cls).stringify();
    std::string mn = yam::JsonValue(m.name).stringify();
    std::string args = buildArgsJSON(m);
    unsigned long long ih = 0;
    { std::lock_guard<std::mutex> lk(mu_);
      auto it = instances_.find(cls);
      if (it != instances_.end() && selectedInstance_ >= 0 && selectedInstance_ < (int)it->second.size())
          ih = it->second[selectedInstance_].handle; }
    std::string js = "(function(){try{var C=Java.use(" + cn + ");var MM=C[" + mn + "];"
        "if(!MM||!MM.overloads)throw new Error('no overloads');"
        "var spec=" + args + ";var a=spec.map(mat);"
        "var isStatic=" + std::string(m.isStatic?"true":"false") + ";"
        "var instH=" + std::to_string(ih) + ";var inst=null;"
        "if(!isStatic){if(instH>0){inst=get(instH);}"
        "else{Java.choose(" + cn + ",{onMatch:function(x){inst=x;return 'stop';},onComplete:function(){}});}"
        "if(!inst)throw new Error('no instance');}"
        "var ov=MM.overloads[0];var r=(isStatic?ov.apply(null,a):ov.apply(inst,a));"
        "send({type:'call_result',result:describe(r)});"
        "}catch(e){send({type:'call_result',result:'ERR: '+e});}})();";
    sendJS(js);
}

void ClassBrowser::findInstances(const std::string& cls) {
    std::string cn = yam::JsonValue(cls).stringify();
    std::string js = "(function(){try{var items=[];Java.choose(" + cn + ",{"
        "onMatch:function(x){try{var h=createHandle(x,'java'," + cn + ");var fields=[];"
        "try{var cls=Java.use(String(x.$className));var flds=cls.class.getDeclaredFields();"
        "for(var i=0;i<flds.length&&i<32;i++){var f=flds[i];if((f.getModifiers()&8)!==0)continue;"
        "try{f.setAccessible(true);var fv=f.get(x);fields.push({name:String(f.getName()),value:describe(fv)});}catch(e){}}}catch(e){}"
        "items.push({handle:h,className:String(x.$className),fields:fields});}catch(e){}"
        "if(items.length>=30)return 'stop';},onComplete:function(){"
        "send({type:'instances_result',className:" + cn + ",items:items});}});"
        "}catch(e){send({type:'instances_result',className:" + cn + ",items:[]});}})();";
    sendJS(js);
}

} // namespace yamgg
