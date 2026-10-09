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
        auto* a = ev.data.get("items");
        if (a && a->is_arr()) for (auto& e : a->arr_val) classes_.push_back(e.as_str());
        std::sort(classes_.begin(), classes_.end());
        loading_ = false;
        LOGI("CB: %zu classes", classes_.size());
    });

    yam::events::on("class_detail_result", [this](const yam::Event& ev) {
        std::string cls = ev.get_str("className");
        std::lock_guard<std::mutex> lk(mu_);
        // methods
        std::vector<MethodInfo> ml;
        auto* ms = ev.data.get("methods");
        if (ms && ms->is_arr()) {
            for (auto& it : ms->arr_val) {
                MethodInfo mi;
                if (auto* n = it.get("name")) mi.name = n->as_str();
                if (auto* r = it.get("ret")) mi.ret = r->as_str();
                if (auto* is = it.get("isStatic")) mi.isStatic = is->as_bool(true);
                if (auto* ar = it.get("args")) if (ar->is_arr()) {
                    for (auto& e : ar->arr_val) {
                        ParamInfo pi;
                        if (auto* tn = e.get("typeName")) pi.typeName = tn->as_str();
                        if (auto* kd = e.get("kind")) pi.kind = kd->as_str();
                        if (auto* ev2 = e.get("enumValues")) if (ev2->is_arr())
                            for (auto& x : ev2->arr_val) pi.enumValues.push_back(x.as_str());
                        pi.scalar[0] = 0;
                        mi.args.push_back(std::move(pi));
                    }
                }
                ml.push_back(std::move(mi));
            }
        }
        methods_[cls] = std::move(ml);
        // fields
        std::vector<FieldInfo> fl;
        auto* fs = ev.data.get("fields");
        if (fs && fs->is_arr()) {
            for (auto& it : fs->arr_val) {
                FieldInfo fi;
                if (auto* n = it.get("name")) fi.name = n->as_str();
                if (auto* t = it.get("type")) fi.type = t->as_str();
                if (auto* v = it.get("value")) fi.value = v->as_str();
                fl.push_back(std::move(fi));
            }
        }
        fields_[cls] = std::move(fl);
        selectedClass_ = cls;
        loading_ = false;
    });

    auto err = [this](const yam::Event&) { std::lock_guard<std::mutex> lk(mu_); loading_ = false; };
    yam::events::on("classes_error", err);
    yam::events::on("class_detail_error", err);

    yam::events::on("instances_result", [this](const yam::Event& ev) {
        std::string cls = ev.get_str("className");
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<InstanceEntry> list;
        auto* a = ev.data.get("items");
        if (a && a->is_arr()) {
            for (auto& it : a->arr_val) {
                InstanceEntry ie;
                if (auto* h = it.get("handle")) ie.handle = (unsigned long long)h->as_i64(0);
                if (auto* cn = it.get("className")) ie.className = cn->as_str();
                if (auto* fs = it.get("fields")) if (fs->is_arr())
                    for (auto& f : fs->arr_val) {
                        std::string n, v;
                        if (auto* fn = f.get("name")) n = fn->as_str();
                        if (auto* fv = f.get("value")) v = fv->as_str();
                        ie.fields.emplace_back(n, v);
                    }
                list.push_back(std::move(ie));
            }
        }
        instances_[cls] = std::move(list);
        selectedInstance_ = 0;
    });
}

void ClassBrowser::triggerLoadClasses() {
    if (filter_.empty()) filter_ = "com.";
    loading_ = true;
    { std::lock_guard<std::mutex> lk(mu_); classes_.clear(); }
    std::string f = yam::JsonValue(filter_).stringify();
    std::string js = "(function(){try{var all=Java.enumerateLoadedClassesSync();"
        "var f=" + f + ";var out=[];"
        "for(var i=0;i<all.length&&out.length<1500;i++){"
        "if(all[i].indexOf(f)>=0)out.push(all[i]);}"
        "send({type:'classes_result',items:out});"
        "}catch(e){send({type:'classes_error',message:''+e});}})();";
    sendJS(js);
}

void ClassBrowser::triggerLoadMethods(const std::string& cls) {
    loading_ = true;
    std::string cn = yam::JsonValue(cls).stringify();
    std::string js = "(function(){try{var c=Java.use(" + cn + ");"
        "var methods=[];var ms=c.class.getDeclaredMethods();"
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
        "methods.push({name:String(m.getName()),ret:String(m.getReturnType().getName()),isStatic:isStatic,args:args});"
        "}catch(e){}}"
        "var fields=[];var flds=c.class.getDeclaredFields();"
        "for(var k=0;k<flds.length;k++){try{var f=flds[k];f.setAccessible(true);"
        "fields.push({name:String(f.getName()),type:String(f.getType().getName()),value:''});}catch(e){}}"
        "send({type:'class_detail_result',className:" + cn + ",methods:methods,fields:fields});"
        "}catch(e){send({type:'class_detail_error',message:''+e});}})();";
    sendJS(js);
}

// (trace/call/findInstances omitted for brevity — same as before)
void ClassBrowser::triggerTrace(const std::string& cls, MethodInfo& m) {
    // same as before
    std::string cn = yam::JsonValue(cls).stringify();
    std::string mn = yam::JsonValue(m.name).stringify();
    std::string js = "(function(){try{var C=Java.use(" + cn + ");var MM=C[" + mn + "];"
        "if(!MM||!MM.overloads)throw new Error('no overloads');"
        "for(var i=0;i<MM.overloads.length;i++){(function(ov){var o=ov.implementation;ov.__ygg=o;"
        "ov.implementation=function(){var a=Array.prototype.slice.call(arguments);"
        "var s=[];for(var k=0;k<a.length;k++)s.push(describe(a[k]));"
        "send({type:'console',level:'log',line:'[trace] " + cls + "." + m.name + " ('+s.join(', ')+')'});"
        "var r=ov.call(this,...a);return r;};})(MM.overloads[i]);}"
        "send({type:'trace_on_ok'});}catch(e){send({type:'trace_err',message:''+e});}})();";
    sendJS(js);
    m.tracing = true;
}
void ClassBrowser::triggerCall(const std::string& cls, MethodInfo& m) {
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
        "var isS=" + std::string(m.isStatic?"true":"false") + ";"
        "var instH=" + std::to_string(ih) + ";var inst=null;"
        "if(!isS){if(instH>0){inst=get(instH);}"
        "else{Java.choose(" + cn + ",{onMatch:function(x){inst=x;return 'stop';},onComplete:function(){}});}"
        "if(!inst)throw new Error('no instance');}"
        "var ov=MM.overloads[0];var r=(isS?ov.apply(null,a):ov.apply(inst,a));"
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

std::string ClassBrowser::buildArgsJSON(const MethodInfo& m) {
    std::string out = "[";
    for (size_t i = 0; i < m.args.size(); ++i) {
        if (i) out += ",";
        const ParamInfo& p = m.args[i];
        std::string k = p.kind;
        if (k=="int"||k=="long"||k=="short"||k=="byte"||k=="float"||k=="double")
            out += "{\"kind\":\"" + k + "\",\"value\":" + (p.scalar[0]?p.scalar:"0") + "}";
        else if (k=="boolean")
            out += std::string("{\"kind\":\"boolean\",\"value\":") + (p.booleanVal?"true":"false") + "}";
        else if (k=="char")
            out += "{\"kind\":\"char\",\"value\":" + yam::JsonValue(std::string(p.scalar[0]?p.scalar:"a")).stringify() + "}";
        else if (k=="string")
            out += "{\"kind\":\"string\",\"value\":" + yam::JsonValue(std::string(p.scalar)).stringify() + "}";
        else if (k=="enum")
            out += "{\"kind\":\"enum\",\"className\":" + yam::JsonValue(p.typeName).stringify() +
                   ",\"enumName\":" + yam::JsonValue(std::string(p.scalar)).stringify() + "}";
        else if (k=="map") {
            out += "{\"kind\":\"map\",\"className\":" + yam::JsonValue(p.typeName).stringify() + ",\"entries\":[";
            for (size_t j = 0; j < p.kvs.size(); ++j) {
                if (j) out += ",";
                out += "{\"k\":" + yam::JsonValue(std::string(p.kvs[j].k)).stringify() +
                       ",\"v\":" + yam::JsonValue(std::string(p.kvs[j].v)).stringify() + "}";
            }
            out += "]}";
        } else if (k=="array")
            out += "{\"kind\":\"arrayJson\",\"className\":" + yam::JsonValue(p.typeName).stringify() +
                   ",\"json\":\"" + yam::str::escape_json(std::string(p.scalar)) + "\"}";
        else
            out += "{\"kind\":\"expr\",\"value\":" + yam::JsonValue(std::string(p.scalar)).stringify() + "}";
    }
    out += "]";
    return out;
}

void ClassBrowser::draw() {
    drawSearchBar();
    ImGui::Separator();
    ImGui::BeginChild("##TreePanel", ImVec2(0, 0), true);
    drawTree();
    ImGui::EndChild();
}

void ClassBrowser::drawSearchBar() {
    ImGui::Spacing();
    // Checkboxes
    ImGui::Checkbox("Class", &searchClass_);
    ImGui::SameLine(); ImGui::Checkbox("Method", &searchMethod_);
    ImGui::SameLine(); ImGui::Checkbox("Field", &searchField_);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-100);
    if (ImGui::InputTextWithHint("##search", "search...", searchBuf_, sizeof(searchBuf_),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
        filter_ = searchBuf_;
        if (filter_.empty()) filter_ = "com.";
        triggerLoadClasses();
    }
    ImGui::SameLine();
    if (ImGui::Button("Find", ImVec2(80, 0))) {
        filter_ = searchBuf_.empty() ? "com." : searchBuf_;
        triggerLoadClasses();
    }
    if (loading_) { ImGui::SameLine(); ImGui::TextDisabled("..."); }
}

void ClassBrowser::drawTree() {
    std::lock_guard<std::mutex> lk(mu_);
    if (classes_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("No results");
        ImGui::PopTextWrapPos();
        return;
    }
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
    bool open = ImGui::TreeNodeEx(pkg.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopTextWrapPos();
    if (!open) return;
    for (auto& full : members) {
        size_t dot = full.find_last_of('.');
        std::string simple = (dot == std::string::npos) ? full : full.substr(dot + 1);
        ImGuiTreeNodeFlags cf = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (selectedClass_ == full) cf |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushID(full.c_str());
        ImGui::PushTextWrapPos(0.0f);
        bool cOpen = ImGui::TreeNodeEx(simple.c_str(), cf);
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (selectedClass_ != full) triggerLoadMethods(full);
        }
        if (cOpen) { drawClassNode(full); ImGui::TreePop(); }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void ClassBrowser::drawClassNode(const std::string& cls) {
    // Fields table first
    auto fit = fields_.find(cls);
    if (fit != fields_.end() && !fit->second.empty()) {
        if (ImGui::TreeNodeEx("Fields", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            if (ImGui::BeginTable("##fields", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("name");
                ImGui::TableSetupColumn("type");
                ImGui::TableSetupColumn("value");
                ImGui::TableHeadersRow();
                for (auto& f : fit->second) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", f.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", f.type.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", f.value.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }
    // Methods
    auto mit = methods_.find(cls);
    if (mit != methods_.end()) {
        for (auto& m : mit->second) drawMethodNode(cls, m);
    }
}

void ClassBrowser::drawMethodNode(const std::string& cls, MethodInfo& m) {
    ImGui::PushID(&m);
    char label[512];
    std::snprintf(label, sizeof(label), "%s %s(%zu)%s",
        m.ret.c_str(), m.name.c_str(), m.args.size(), m.tracing ? " [T]" : "");
    ImGui::PushTextWrapPos(0.0f);
    bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopTextWrapPos();
    if (open) {
        for (size_t i = 0; i < m.args.size(); ++i) drawParamWidget(cls, m, m.args[i], (int)i);
        ImGui::Separator();
        if (!m.isStatic) drawInstancePicker(cls, m);
        if (ImGui::Button(m.tracing ? "Stop" : "Trace", ImVec2(110, 38)))
            m.tracing ? (triggerTrace(cls, m), m.tracing=false) : triggerTrace(cls, m);
        ImGui::SameLine();
        if (ImGui::Button("Call", ImVec2(110, 38))) triggerCall(cls, m);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ClassBrowser::drawParamWidget(const std::string&, MethodInfo&, ParamInfo& p, int idx) {
    ImGui::PushID(idx);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("arg%d %s", idx, p.typeName.c_str());
    ImGui::PopTextWrapPos();
    const std::string& k = p.kind;
    if (k == "boolean") ImGui::Checkbox("##b", &p.booleanVal);
    else if (k == "enum") {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##e", p.scalar[0] ? p.scalar : "")) {
            for (auto& v : p.enumValues)
                if (ImGui::Selectable(v.c_str(), v == p.scalar))
                    std::strncpy(p.scalar, v.c_str(), sizeof(p.scalar)-1);
            ImGui::EndCombo();
        }
    } else if (k == "map") {
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
        if (ImGui::SmallButton("+ add")) p.kvs.push_back({});
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
        if (ImGui::Button("Find Instances", ImVec2(180, 36))) findInstances(cls);
        return;
    }
    for (size_t i = 0; i < it->second.size(); ++i) {
        ImGui::PushID((int)i);
        char lbl[128]; std::snprintf(lbl, sizeof(lbl), "#%zu %s", i, it->second[i].className.c_str());
        bool sel = ((int)i == selectedInstance_);
        ImGui::PushTextWrapPos(0.0f);
        if (ImGui::Selectable(lbl, sel)) selectedInstance_ = (int)i;
        ImGui::PopTextWrapPos();
        if (sel && ImGui::BeginTable("##if", 2, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("field"); ImGui::TableSetupColumn("value");
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

} // namespace yamgg
