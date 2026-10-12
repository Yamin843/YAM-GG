#include "ClassBrowser.h"
#include "JSConsole.h"
#include "../../Bridge/YamBridge.h"

#include "imgui.h"
#include "yam.hpp"

#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {
std::string jesc(const std::string& s) {
    std::string out; out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += R"(\")";  break;
        case '\\': out += R"(\\)";  break;
        case '\n': out += R"(\n)";  break;
        case '\r': out += R"(\r)";  break;
        case '\t': out += R"(\t)";  break;
        default:   out += c;
        }
    }
    return out;
}
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}
} // anon

ClassBrowser& ClassBrowser::instance() { static ClassBrowser i; return i; }
ClassBrowser::ClassBrowser() { registerEvents(); }
ClassBrowser::~ClassBrowser() = default;

void ClassBrowser::sendCommand(const std::string& json) {
    YamBridge::instance().postCommand(json);
}

void ClassBrowser::pushTraceLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    if (traceLogPaused_) return;
    traceLog_.push_back(line);
    if (traceLog_.size() > 5000)
        traceLog_.erase(traceLog_.begin(), traceLog_.begin() + 1000);
}

// ═══════════════════════════════════════════════════════════════════════════
// Event routing
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::registerEvents() {
    if (eventsRegistered_) return;
    eventsRegistered_ = true;

    if (!chunksRegistered_) {
        chunksRegistered_ = true;
        yam::chunks::set_full_handler(
            [](const std::string& kind, const std::string& json) {
                auto parsed = yam::JsonValue::parse(json);
                if (!parsed) return;
                yam::Event ev;
                ev.type = kind;
                ev.data = std::move(parsed.value());
                ev.timestamp = yam::time_util::now_ms();
                yam::events::dispatch(ev);
            });
    }

    yam::events::on("classes_list", [this](const yam::Event& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        classes_.clear();
        auto* a = ev.data.get("classes");
        if (a && a->is_arr())
            for (auto& e : a->arr_val) classes_.push_back(e.as_str());
        std::sort(classes_.begin(), classes_.end());
        loading_ = false;
        lastQuery_ = "\x01INVALIDATE\x01";
        LOGI("CB: %zu classes", classes_.size());
    });

    yam::events::on("class_detail_result", [this](const yam::Event& ev) {
        std::string cls = ev.get_str("className");
        std::lock_guard<std::mutex> lk(mu_);

        std::vector<MethodInfo> ml;
        auto* ms = ev.data.get("methods");
        if (ms && ms->is_arr()) {
            for (auto& it : ms->arr_val) {
                MethodInfo mi;
                if (auto* n = it.get("name")) mi.name = n->as_str();
                if (auto* r = it.get("ret"))  mi.ret  = r->as_str();
                if (auto* rk = it.get("retKind")) mi.retKind = rk->as_str();
                if (auto* is = it.get("isStatic")) mi.isStatic = is->as_bool(true);
                if (auto* ar = it.get("args")) if (ar->is_arr()) {
                    for (auto& e : ar->arr_val) {
                        ParamInfo pi;
                        if (auto* tn = e.get("typeName")) pi.typeName = tn->as_str();
                        if (auto* kd = e.get("kind"))     pi.kind = kd->as_str();
                        if (auto* ge = e.get("generic"))  pi.generic = ge->as_str();
                        if (auto* ev2 = e.get("enumValues")) if (ev2->is_arr())
                            for (auto& x : ev2->arr_val)
                                pi.enumValues.push_back(x.as_str());
                        mi.args.push_back(std::move(pi));
                    }
                }
                ml.push_back(std::move(mi));
            }
        }
        for (auto& mi : ml) {
            std::string key = cls + "::" + mi.name;
            if (tracedMethods_.count(key)) mi.tracing = true;
        }
        std::sort(ml.begin(), ml.end(),
            [](const MethodInfo& a, const MethodInfo& b) { return a.name < b.name; });
        methods_[cls] = std::move(ml);

        std::vector<FieldInfo> fl;
        auto* fs = ev.data.get("fields");
        if (fs && fs->is_arr()) {
            for (auto& it : fs->arr_val) {
                FieldInfo fi;
                if (auto* n = it.get("name"))     fi.name     = n->as_str();
                if (auto* t = it.get("type"))     fi.type     = t->as_str();
                if (auto* tk = it.get("typeKind")) fi.typeKind = tk->as_str();
                if (auto* v = it.get("value"))    fi.value    = v->as_str();
                if (auto* s = it.get("isStatic")) fi.isStatic = s->as_bool(false);
                fl.push_back(std::move(fi));
            }
        }
        std::sort(fl.begin(), fl.end(),
            [](const FieldInfo& a, const FieldInfo& b) { return a.name < b.name; });
        fields_[cls] = std::move(fl);

        std::vector<CtorInfo> cl;
        auto* cs = ev.data.get("constructors");
        if (cs && cs->is_arr()) {
            for (auto& it : cs->arr_val) {
                CtorInfo ci;
                if (auto* ar = it.get("args")) if (ar->is_arr()) {
                    for (auto& e : ar->arr_val) {
                        ParamInfo pi;
                        if (auto* tn = e.get("typeName")) pi.typeName = tn->as_str();
                        if (auto* kd = e.get("kind"))     pi.kind = kd->as_str();
                        ci.args.push_back(std::move(pi));
                    }
                }
                cl.push_back(std::move(ci));
            }
        }
        ctors_[cls] = std::move(cl);

        selectedClass_ = cls;
        loading_ = false;
        pendingClasses_.erase(cls);
        LOGI("CB: class_detail %s m=%zu f=%zu c=%zu", cls.c_str(),
             methods_[cls].size(), fields_[cls].size(), ctors_[cls].size());
    });

    yam::events::on("instances_result", [this](const yam::Event& ev) {
        std::string cls = ev.get_str("className");
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<InstanceEntry> list;
        auto* a = ev.data.get("items");
        if (a && a->is_arr()) {
            for (auto& it : a->arr_val) {
                InstanceEntry ie;
                if (auto* h  = it.get("handle"))    ie.handle = (unsigned long long)h->as_i64(0);
                if (auto* cn = it.get("className")) ie.className = cn->as_str();
                if (auto* fs = it.get("fields")) if (fs->is_arr()) {
                    for (auto& f : fs->arr_val) {
                        std::string n, v;
                        if (auto* fn = f.get("name"))  n = fn->as_str();
                        if (auto* fv = f.get("value")) v = fv->as_str();
                        ie.fields.emplace_back(n, v);
                    }
                }
                list.push_back(std::move(ie));
            }
        }
        instances_[cls] = std::move(list);
        selectedInstance_ = 0;
    });

    yam::events::on("call_result", [this](const yam::Event& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        callState_.haveResult = true;
        callState_.lastOk = ev.get_bool("ok", false);
        if (callState_.lastOk)
            callState_.lastResult = ev.get_str("result");
        else
            callState_.lastResult = "ERR: " + ev.get_str("error");
    });

    yam::events::on("trace_line", [this](const yam::Event& ev) {
        std::string line = ev.get_str("line");
        if (!line.empty()) {
            pushTraceLog(line);
            JSConsole::instance().pushOutput(line);
        }
    });

    yam::events::on("trace_ok", [this](const yam::Event& ev) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            R"({"t":"trace_ok","cls":"%s","m":"%s","n":%d})",
            ev.get_str("className").c_str(),
            ev.get_str("methodName").c_str(),
            (int)ev.get_i64("count", 0));
        pushTraceLog(buf);
    });
    yam::events::on("trace_err", [this](const yam::Event& ev) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            R"({"t":"trace_err","err":"%s"})",
            jesc(ev.get_str("error")).c_str());
        pushTraceLog(buf);
    });
    yam::events::on("field_read", [this](const yam::Event& ev) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            R"({"t":"field_read","cls":"%s","f":"%s","v":"%s"})",
            jesc(ev.get_str("className")).c_str(),
            jesc(ev.get_str("fieldName")).c_str(),
            jesc(ev.get_str("value")).c_str());
        pushTraceLog(buf);
    });
    yam::events::on("field_write", [this](const yam::Event& ev) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            R"({"t":"field_write","cls":"%s","f":"%s","new":"%s"})",
            jesc(ev.get_str("className")).c_str(),
            jesc(ev.get_str("fieldName")).c_str(),
            jesc(ev.get_str("value")).c_str());
        pushTraceLog(buf);
    });
    yam::events::on("members_search_result", [this](const yam::Event& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        memberMatches_.clear();
        auto* arr = ev.data.get("matches");
        if (arr && arr->is_arr()) {
            for (auto& m : arr->arr_val) {
                MemberMatch mm;
                if (auto* v = m.get("className")) mm.className = v->as_str();
                if (auto* v = m.get("name"))      mm.name      = v->as_str();
                if (auto* v = m.get("kind"))      mm.kind      = v->as_str();
                if (auto* v = m.get("ret"))       mm.ret       = v->as_str();
                if (auto* v = m.get("argCount"))  mm.argCount  = (int)v->as_i64(0);
                if (auto* v = m.get("isStatic"))  mm.isStatic  = v->as_bool(false);
                memberMatches_.push_back(std::move(mm));
            }
        }
        LOGI("CB: members_search %s -> %zu", ev.get_str("query").c_str(),
             memberMatches_.size());
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Command triggers
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::triggerLoadClasses(bool force) {
    if (!force) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!classes_.empty()) {
            lastQuery_ = "\x01INVALIDATE\x01";
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        classes_.clear();
        filtered_.clear();
        lastQuery_.clear();
    }
    loading_ = true;
    LOGI("CB: triggerLoadClasses force=%d", (int)force);
    sendCommand(R"({"action":"jv_enumerate","filter":""})");
}

void ClassBrowser::triggerLoadMethods(const std::string& cls) {
    loading_ = true;
    std::string cmd = R"({"action":"jv_detail","className":")";
    cmd += jesc(cls); cmd += R"("})";
    sendCommand(cmd);
}

void ClassBrowser::triggerTrace(const std::string& cls, MethodInfo& m) {
    std::string cmd = R"({"action":"jv_trace","className":")";
    cmd += jesc(cls); cmd += R"(","methodName":")"; cmd += jesc(m.name);
    cmd += R"(","argIndex":-1})";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    m.tracing = true; m.argTraceIdx = -1;
    tracedMethods_.insert(cls + "::" + m.name);
}

void ClassBrowser::triggerTraceArg(const std::string& cls, MethodInfo& m, int idx) {
    std::string cmd = R"({"action":"jv_trace","className":")";
    cmd += jesc(cls); cmd += R"(","methodName":")"; cmd += jesc(m.name);
    cmd += R"(","argIndex":)"; cmd += std::to_string(idx); cmd += "}";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    m.tracing = true; m.argTraceIdx = idx;
    tracedMethods_.insert(cls + "::" + m.name);
}

void ClassBrowser::triggerUntrace(const std::string& cls, MethodInfo& m) {
    std::string cmd = R"({"action":"jv_untrace","className":")";
    cmd += jesc(cls); cmd += R"(","methodName":")"; cmd += jesc(m.name);
    cmd += R"("})";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    m.tracing = false; m.argTraceIdx = -1;
    tracedMethods_.erase(cls + "::" + m.name);
}

void ClassBrowser::triggerFieldRead(const std::string& cls, FieldInfo& f) {
    std::string cmd = R"({"action":"jv_field_read","className":")";
    cmd += jesc(cls); cmd += R"(","fieldName":")"; cmd += jesc(f.name);
    cmd += R"("})";
    sendCommand(cmd);
}

void ClassBrowser::triggerFieldWatch(const std::string& cls, FieldInfo& f) {
    std::string cmd = R"({"action":"jv_field_watch","className":")";
    cmd += jesc(cls); cmd += R"(","fieldName":")"; cmd += jesc(f.name);
    cmd += R"(","intervalMs":500})";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    f.watching = true;
}

void ClassBrowser::triggerFieldUnwatch(const std::string& cls, FieldInfo& f) {
    std::string cmd = R"({"action":"jv_field_unwatch","className":")";
    cmd += jesc(cls); cmd += R"(","fieldName":")"; cmd += jesc(f.name);
    cmd += R"("})";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    f.watching = false;
}

void ClassBrowser::findInstances(const std::string& cls) {
    std::string cmd = R"({"action":"jv_instances","className":")";
    cmd += jesc(cls); cmd += R"("})";
    sendCommand(cmd);
}

void ClassBrowser::triggerMemberSearch() {
    std::string q;
    { std::lock_guard<std::mutex> lk(mu_); q = searchBuf_; }
    if (q.size() < 2) return;
    std::string cmd = R"({"action":"jv_search_members","query":")";
    cmd += jesc(q);
    cmd += R"(","wantMethod":)"; cmd += (searchMethod_ ? "true" : "false");
    cmd += R"(,"wantField":)";  cmd += (searchField_  ? "true" : "false");
    cmd += "}";
    sendCommand(cmd);
}

void ClassBrowser::activateCall(const std::string& cls, MethodInfo& m) {
    std::lock_guard<std::mutex> lk(mu_);
    callState_.active    = true;
    callState_.isCtor    = false;
    callState_.className = cls;
    callState_.methodName= m.name;
    callState_.returnType= m.ret;
    callState_.isStatic  = m.isStatic;
    callState_.args      = m.args;
    callState_.lastResult.clear();
    callState_.haveResult = false;
    pendingSwitchToCall_ = true;
    pendingSubTab_ = 2;
}

void ClassBrowser::activateCallCtor(const std::string& cls, CtorInfo& c) {
    std::lock_guard<std::mutex> lk(mu_);
    callState_.active    = true;
    callState_.isCtor    = true;
    callState_.className = cls;
    callState_.methodName= "<init>";
    callState_.returnType= "void";
    callState_.isStatic  = false;
    callState_.args      = c.args;
    callState_.lastResult.clear();
    callState_.haveResult = false;
    pendingSwitchToCall_ = true;
}

void ClassBrowser::fireCall() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!callState_.active) return;
    std::string args = argsSpecFor(callState_.args);
    std::string cmd;
    if (callState_.isCtor) {
        cmd  = R"({"action":"jv_new","className":")";
        cmd += jesc(callState_.className);
        cmd += R"(","argsSpec":)"; cmd += args; cmd += "}";
    } else {
        unsigned long long ih = 0;
        auto it = instances_.find(callState_.className);
        if (it != instances_.end() && selectedInstance_ >= 0
            && selectedInstance_ < (int)it->second.size())
            ih = it->second[selectedInstance_].handle;
        cmd  = R"({"action":"jv_call","className":")";
        cmd += jesc(callState_.className);
        cmd += R"(","methodName":")"; cmd += jesc(callState_.methodName);
        cmd += R"(","isStatic":)"; cmd += (callState_.isStatic ? "true" : "false");
        cmd += R"(,"instanceHandle":)"; cmd += std::to_string(ih);
        cmd += R"(,"argsSpec":)"; cmd += args; cmd += "}";
    }
    callState_.haveResult = false;
    sendCommand(cmd);
}

std::string ClassBrowser::argsSpecFor(const std::vector<ParamInfo>& args) {
    std::string out = "[";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += ",";
        const ParamInfo& p = args[i];
        const std::string& k = p.kind;
        if (k == "int" || k == "long" || k == "short" || k == "byte"
            || k == "atomic_int" || k == "atomic_long" || k == "bignum") {
            out += R"({"kind":")"; out += (k=="atomic_int"?"int":k=="atomic_long"?"long":k=="bignum"?"big":k);
            out += R"(","value":)"; out += (p.scalar[0] ? p.scalar : "0"); out += "}";
        } else if (k == "float" || k == "double" || k == "bigdec") {
            out += R"({"kind":")"; out += (k=="bigdec"?"bigdec":k);
            out += R"(","value":)"; out += (p.scalar[0] ? p.scalar : "0"); out += "}";
        } else if (k == "boolean" || k == "atomic_bool") {
            out += R"({"kind":")"; out += (k=="atomic_bool"?"atomic_bool":"boolean");
            out += R"(","value":)"; out += (p.booleanVal ? "true" : "false"); out += "}";
        } else if (k == "char") {
            out += R"({"kind":"char","value":")";
            char ch = p.scalar[0] ? p.scalar[0] : 'a';
            char tmp[3] = { ch, 0, 0 };
            out += jesc(tmp); out += R"("})";
        } else if (k == "string" || k == "file" || k == "boxed" ||
                   k == "charbuf" || k == "pattern" || k == "uuid" ||
                   k == "url" || k == "uri") {
            std::string jk = "string";
            if (k == "charbuf") jk = "stringbuilder";
            else if (k == "pattern") jk = "pattern";
            out += R"({"kind":")"; out += jk; out += R"(","value":")";
            out += jesc(p.scalar); out += R"("})";
        } else if (k == "enum") {
            out += R"({"kind":"enum","className":")"; out += jesc(p.typeName);
            out += R"(","enumName":")"; out += jesc(p.scalar); out += R"("})";
        } else if (k == "date") {
            out += R"({"kind":"date","value":")";
            out += jesc(p.dateStr[0] ? p.dateStr : "2026-01-01 00:00:00");
            out += R"("})";
        } else if (k == "class" || k == "reflect") {
            out += R"({"kind":"class","value":")";
            out += jesc(p.nestedClass[0] ? p.nestedClass : "java.lang.Object");
            out += R"("})";
        } else if (k == "object") {
            if (p.nestedClass[0]) {
                out += R"({"kind":"object","className":")";
                out += jesc(p.nestedClass); out += R"(","args":[]})";
            } else out += R"({"kind":"null"})";
        } else if (k == "map" || k == "properties") {
            std::string cls = p.typeName;
            if (cls.empty() || cls == "java.util.Map") cls = "java.util.HashMap";
            if (k == "properties") cls = "java.util.Properties";
            out += R"({"kind":"map","className":")"; out += jesc(cls);
            out += R"(","entries":[)";
            for (size_t j = 0; j < p.kvPairs.size(); ++j) {
                if (j) out += ",";
                const KVPair& kv = p.kvPairs[j];
                out += R"({"k":{"kind":"string","value":")"; out += jesc(kv.key);
                out += R"("},"v":)";
                switch (kv.valueType) {
                case 1: out += R"({"kind":"int","value":)"; out += (kv.value[0] ? kv.value : "0"); out += "}"; break;
                case 2: out += R"({"kind":"long","value":)"; out += (kv.value[0] ? kv.value : "0"); out += "}"; break;
                case 3: out += R"({"kind":"double","value":)"; out += (kv.value[0] ? kv.value : "0"); out += "}"; break;
                case 4: out += R"({"kind":"boolean","value":)"; out += (std::strcmp(kv.value,"true")==0 ? "true" : "false"); out += "}"; break;
                case 5: out += R"({"kind":"null"})"; break;
                default: out += R"({"kind":"string","value":")"; out += jesc(kv.value); out += R"("})";
                }
                out += "}";
            }
            out += "]}";
        } else if (k == "list" || k == "set" || k == "queue" ||
                   k == "object_array" || k == "array" ||
                   k == "prim_array" || k == "byte_array" ||
                   k == "iterable" || k == "optional") {
            std::string listKind = "list";
            if (k == "set" || k == "iterable") listKind = "set";
            if (k == "optional") listKind = "list";
            std::string cls = p.typeName;
            if (cls.empty() || cls == "java.util.List") cls = "java.util.ArrayList";
            if (k == "set" && cls == "java.util.Set") cls = "java.util.HashSet";
            if (k == "queue") cls = "java.util.LinkedList";
            if (k == "iterable") cls = "java.util.ArrayList";
            if (k == "optional") cls = "java.util.Optional";
            out += R"({"kind":")"; out += listKind; out += R"(","className":")"; out += jesc(cls);
            out += R"(","items":[)";
            for (size_t j = 0; j < p.listItems.size(); ++j) {
                if (j) out += ",";
                switch (p.listItemType) {
                case 1: out += R"({"kind":"int","value":)"; out += (p.listItems[j].empty() ? "0" : p.listItems[j].c_str()); out += "}"; break;
                case 2: out += R"({"kind":"long","value":)"; out += (p.listItems[j].empty() ? "0" : p.listItems[j].c_str()); out += "}"; break;
                case 3: out += R"({"kind":"double","value":)"; out += (p.listItems[j].empty() ? "0" : p.listItems[j].c_str()); out += "}"; break;
                case 4: out += R"({"kind":"boolean","value":)"; out += (p.listItems[j] == "true" ? "true" : "false"); out += "}"; break;
                default: out += R"({"kind":"string","value":")"; out += jesc(p.listItems[j]); out += R"("})";
                }
            }
            out += "]}";
        } else {
            if (p.jsonSpec[0]) out += p.jsonSpec;
            else               out += R"({"kind":"null"})";
        }
    }
    out += "]";
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main draw
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::draw() {
    if (!autoLoadTried_) { autoLoadTried_ = true; triggerLoadClasses(); }

    ImGuiTabItemFlags f0 = (pendingSubTab_ == 0) ? ImGuiTabItemFlags_SetSelected : 0;
    ImGuiTabItemFlags f1 = (pendingSubTab_ == 1) ? ImGuiTabItemFlags_SetSelected : 0;
    ImGuiTabItemFlags f2 = (pendingSubTab_ == 2) ? ImGuiTabItemFlags_SetSelected : 0;

    if (ImGui::BeginTabBar("##jv_subtabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Classes", nullptr, f0)) {
            activeSubTab_ = 0;
            drawClassesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("TraceLog", nullptr, f1)) {
            activeSubTab_ = 1;
            drawTraceLogTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CallMethod", nullptr, f2)) {
            activeSubTab_ = 2;
            drawCallMethodTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    pendingSubTab_ = -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// Classes tab
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::drawClassesTab() {
    drawSearchBar();
    ImGui::Separator();
    ImGui::BeginChild("##TreePanel", ImVec2(0, 0), true);
    drawMemberMatches();
    drawTree();
    ImGui::EndChild();
}

void ClassBrowser::drawSearchBar() {
    ImGui::Spacing();
    ImGui::Checkbox("Class", &searchClass_);
    ImGui::SameLine(); ImGui::Checkbox("Method", &searchMethod_);
    ImGui::SameLine(); ImGui::Checkbox("Field",  &searchField_);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-100);
    bool enter = ImGui::InputTextWithHint("##search", "search...",
                                           searchBuf_, sizeof(searchBuf_),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool find = ImGui::Button("Find", ImVec2(80, 0));

    if (enter || find) {
        filter_ = searchBuf_;
        triggerLoadClasses(true);
        if (searchMethod_ || searchField_) triggerMemberSearch();
        autoSearchLast_ = searchBuf_;
    }

    // Debounced auto member search while typing
    float now = (float)ImGui::GetTime();
    std::string cur = searchBuf_;
    if (cur != autoSearchLast_) {
        autoSearchLast_ = cur;
        autoSearchAt_ = now + 0.6f;  // 600ms idle before firing
    } else if (!cur.empty() && cur.size() >= 3 &&
               (searchMethod_ || searchField_) &&
               autoSearchAt_ > 0.0f && now >= autoSearchAt_) {
        autoSearchAt_ = 0.0f;
        triggerMemberSearch();
    }

    if (loading_) { ImGui::SameLine(); ImGui::TextDisabled("..."); }
    else if (searchMethod_ || searchField_) {
        ImGui::SameLine();
        ImGui::TextDisabled("(member search active)");
    }
}

void ClassBrowser::drawMemberMatches() {
    if (memberMatches_.empty()) return;
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "MEMBER MATCHES (%zu)", memberMatches_.size());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.42f, 1.0f));
    ImGui::TextUnformatted(hdr);
    ImGui::PopStyleColor();
    ImGui::BeginChild("##memmatches", ImVec2(0, 200), true);
    for (size_t i = 0; i < memberMatches_.size(); ++i) {
        MemberMatch& m = memberMatches_[i];
        ImGui::PushID((int)i);
        char label[512];
        if (m.kind == "method")
            std::snprintf(label, sizeof(label), "[M] %s::%s(%d)->%s",
                          m.className.c_str(), m.name.c_str(),
                          m.argCount, m.ret.c_str());
        else
            std::snprintf(label, sizeof(label), "[F] %s::%s : %s",
                          m.className.c_str(), m.name.c_str(), m.ret.c_str());
        if (ImGui::Selectable(label)) {
            selectedClass_ = m.className;
            if (methods_.find(m.className) == methods_.end() &&
                !pendingClasses_.count(m.className)) {
                pendingClasses_.insert(m.className);
                triggerLoadMethods(m.className);
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();
}

void ClassBrowser::drawTree() {
    std::lock_guard<std::mutex> lk(mu_);
    std::string q = searchBuf_;
    if (q != lastQuery_) {
        lastQuery_ = q;
        filtered_.clear();
        filtered_.reserve(classes_.size());
        std::string qlow = q;
        for (auto& ch : qlow) ch = (char)std::tolower((unsigned char)ch);
        if (qlow.empty()) filtered_ = classes_;
        else {
            for (auto& c : classes_) {
                std::string cl = c;
                for (auto& ch : cl) ch = (char)std::tolower((unsigned char)ch);
                if (cl.find(qlow) != std::string::npos) filtered_.push_back(c);
            }
        }
        LOGI("CB: filtered %zu / %zu (q=%s)", filtered_.size(),
             classes_.size(), q.c_str());
    }
    if (filtered_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (classes_.empty()) ImGui::TextWrapped("Loading classes...");
        else ImGui::TextWrapped("No match");
        ImGui::PopTextWrapPos();
        return;
    }
    std::string lastPkg;
    std::vector<std::string> members;
    auto flush = [&]() {
        if (!members.empty()) { drawPackageGroup(lastPkg, members); members.clear(); }
    };
    for (auto& c : filtered_) {
        size_t dot = c.find_last_of('.');
        std::string pkg = (dot == std::string::npos) ? "<default>" : c.substr(0, dot);
        if (pkg != lastPkg) { flush(); lastPkg = pkg; }
        members.push_back(c);
    }
    flush();
}

void ClassBrowser::drawPackageGroup(const std::string& pkg,
                                     std::vector<std::string>& members) {
    ImGui::PushTextWrapPos(0.0f);
    bool open = ImGui::TreeNodeEx(pkg.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopTextWrapPos();
    if (!open) return;
    for (auto& full : members) {
        size_t dot = full.find_last_of('.');
        std::string simple = (dot == std::string::npos) ? full : full.substr(dot + 1);
        ImGuiTreeNodeFlags cf = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedClass_ == full) cf |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushID(full.c_str());
        if (selectedClass_ == full)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.86f, 0.35f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.90f, 1.0f));
        ImGui::PushTextWrapPos(0.0f);
        bool cOpen = ImGui::TreeNodeEx(simple.c_str(), cf);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (cOpen) {
            if (methods_.find(full) == methods_.end() &&
                !pendingClasses_.count(full)) {
                pendingClasses_.insert(full);
                selectedClass_ = full;
                triggerLoadMethods(full);
            }
            drawClassNode(full);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void ClassBrowser::drawClassNode(const std::string& cls) {
    // member filter
    ImGui::PushID((cls + "#ms").c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ms", "filter members...",
                              memberSearch_, sizeof(memberSearch_));
    ImGui::PopID();

    std::string q = memberSearch_;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);
    auto match = [&](const std::string& name) -> bool {
        if (q.empty()) return true;
        std::string n = name;
        for (auto& c : n) c = (char)std::tolower((unsigned char)c);
        return n.find(q) != std::string::npos;
    };

    // Fields
    auto fit = fields_.find(cls);
    if (fit != fields_.end() && !fit->second.empty()) {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Fields (%zu)###fld", fit->second.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (auto& f : fit->second) if (match(f.name)) drawFieldNode(cls, f);
            ImGui::TreePop();
        }
    }

    // Constructors
    auto cit = ctors_.find(cls);
    if (cit != ctors_.end() && !cit->second.empty()) {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Constructors (%zu)###ctr", cit->second.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (auto& c : cit->second) drawCtorNode(cls, c);
            ImGui::TreePop();
        }
    }

    // Methods
    auto mit = methods_.find(cls);
    if (mit != methods_.end() && !mit->second.empty()) {
        size_t shown = 0;
        for (auto& m : mit->second) if (match(m.name)) shown++;
        if (ImGui::Button("TRACE ALL", ImVec2(120, 30))) {
            for (auto& m : mit->second) {
                if (!m.tracing) triggerTrace(cls, m);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("STOP ALL", ImVec2(120, 30))) {
            for (auto& m : mit->second) {
                if (m.tracing) triggerUntrace(cls, m);
            }
        }
        ImGui::Separator();
        char hdr[80];
        std::snprintf(hdr, sizeof(hdr), "Methods (%zu/%zu)###mth", shown,
                      mit->second.size());
        if (ImGui::TreeNodeEx(hdr,
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (auto& m : mit->second) if (match(m.name)) drawMethodNode(cls, m);
            ImGui::TreePop();
        }
    }
    if (methods_.find(cls) == methods_.end()) {
        ImGui::TextDisabled("loading members...");
    }
}

void ClassBrowser::drawFieldNode(const std::string& cls, FieldInfo& f) {
    ImGui::PushID(&f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s : %s", f.name.c_str(), f.type.c_str());
    ImGui::PopTextWrapPos();
    if (!f.value.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.82f, 0.62f, 1.0f));
        ImGui::Text("= %s", f.value.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
    if (ImGui::SmallButton("READ")) triggerFieldRead(cls, f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (f.watching) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.32f, 0.16f, 1.0f));
        if (ImGui::SmallButton("UNWATCH")) triggerFieldUnwatch(cls, f);
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
        if (ImGui::SmallButton("WATCH")) triggerFieldWatch(cls, f);
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

void ClassBrowser::drawCtorNode(const std::string& cls, CtorInfo& c) {
    ImGui::PushID(&c);
    char label[512];
    std::snprintf(label, sizeof(label), "new %s(%zu)", cls.c_str(), c.args.size());
    if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth)) {
        // show param types compactly
        for (size_t i = 0; i < c.args.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::BulletText("arg%zu: %s", i, c.args[i].typeName.c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
        if (ImGui::Button("CALL", ImVec2(110, 32))) activateCallCtor(cls, c);
        ImGui::PopStyleColor();
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ClassBrowser::drawMethodNode(const std::string& cls, MethodInfo& m) {
    ImGui::PushID(&m);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float lineH = ImGui::GetTextLineHeight();
        ImU32 dotColor = m.tracing ? IM_COL32(0x66, 0xBB, 0x6A, 255)
                                    : IM_COL32(0x62, 0x62, 0x68, 200);
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + 6.0f, p.y + lineH * 0.5f), 4.0f, dotColor);
        ImGui::Dummy(ImVec2(18.0f, 0.0f));
        ImGui::SameLine(0, 0);
    }
    char label[1024];
    std::snprintf(label, sizeof(label), "%s  %s(%zu)",
                  m.ret.c_str(), m.name.c_str(), m.args.size());
    ImGui::PushStyleColor(ImGuiCol_Text,
        m.tracing ? ImVec4(0.92f, 0.92f, 0.94f, 1.0f)
                  : ImVec4(0.82f, 0.82f, 0.85f, 1.0f));
    ImGui::PushTextWrapPos(0.0f);
    bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (open) {
        // compact params — type + TRACE button each
        for (size_t i = 0; i < m.args.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::BulletText("arg%zu: %s", i, m.args[i].typeName.c_str());
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.14f, 0.05f, 1.0f));
            if (ImGui::SmallButton("TRACE##a")) triggerTraceArg(cls, m, (int)i);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::Separator();
        if (m.tracing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.32f, 0.16f, 1.0f));
            if (ImGui::Button("STOP", ImVec2(110, 36))) triggerUntrace(cls, m);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
            if (ImGui::Button("TRACE", ImVec2(110, 36))) triggerTrace(cls, m);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.42f, 1.0f));
        if (ImGui::Button("CALL", ImVec2(110, 36))) activateCall(cls, m);
        ImGui::PopStyleColor(2);
        if (!m.isStatic) {
            ImGui::SameLine();
            if (ImGui::Button("Find Instances", ImVec2(150, 36)))
                findInstances(cls);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// TraceLog tab
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::drawTraceLogTab() {
    ImGui::BeginChild("##tl_top", ImVec2(0, 40), false);
    if (ImGui::Button("Clear", ImVec2(90, 0))) {
        std::lock_guard<std::mutex> lk(mu_);
        traceLog_.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Pause", &traceLogPaused_);
    ImGui::SameLine();
    ImGui::Checkbox("AutoScroll", &traceLogAutoScroll_);
    ImGui::SameLine();
    ImGui::TextDisabled("entries: %zu", traceLog_.size());
    ImGui::EndChild();

    ImGui::BeginChild("##tl_body", ImVec2(0, 0), true,
                       ImGuiWindowFlags_HorizontalScrollbar);
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& line : traceLog_) {
        ImVec4 col(0.88f, 0.88f, 0.92f, 1.0f);
        if (line.find(R"("t":"trace")") != std::string::npos)
            col = ImVec4(0.65f, 0.82f, 0.95f, 1.0f);
        else if (line.find(R"("t":"trace_err")") != std::string::npos)
            col = ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
        else if (line.find(R"("t":"field_read")") != std::string::npos)
            col = ImVec4(0.72f, 0.92f, 0.72f, 1.0f);
        else if (line.find(R"("t":"field_write")") != std::string::npos)
            col = ImVec4(0.98f, 0.86f, 0.42f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }
    if (traceLogAutoScroll_ && !traceLogPaused_ &&
        ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════════════════
// CallMethod tab
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::drawCallMethodTab() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!callState_.active) {
        ImGui::TextDisabled("Click CALL on a method (or constructor) in Classes tab.");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.42f, 1.0f));
    if (callState_.isCtor)
        ImGui::TextWrapped("new %s", callState_.className.c_str());
    else
        ImGui::TextWrapped("%s%s::%s(%zu)",
            callState_.isStatic ? "static " : "",
            callState_.className.c_str(), callState_.methodName.c_str(),
            callState_.args.size());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("return: %s", callState_.returnType.c_str());
    ImGui::Separator();

    if (callState_.args.empty()) {
        ImGui::TextDisabled("(no parameters)");
    } else {
        for (size_t i = 0; i < callState_.args.size(); ++i)
            drawParamWidget(callState_.args[i], (int)i);
    }
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.42f, 1.0f));
    if (ImGui::Button("CALL", ImVec2(120, 40))) fireCall();
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Clear Result", ImVec2(120, 40))) {
        callState_.lastResult.clear();
        callState_.haveResult = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(90, 40))) callState_.active = false;

    if (callState_.haveResult) {
        ImGui::Separator();
        ImGui::TextUnformatted("Result:");
        ImVec4 col = callState_.lastOk
            ? ImVec4(0.65f, 0.92f, 0.65f, 1.0f)
            : ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", callState_.lastResult.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (!callState_.isStatic && !callState_.isCtor)
        drawInstancePicker(callState_.className);
}

// ═══════════════════════════════════════════════════════════════════════════
// Param editor (rich)
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::drawParamWidget(ParamInfo& p, int idx) {
    ImGui::PushID(idx);
    if (!p.generic.empty())
        ImGui::TextWrapped("arg%d %s <%s>", idx, p.typeName.c_str(),
                            p.generic.c_str());
    else
        ImGui::TextWrapped("arg%d (%s) [%s]", idx, p.typeName.c_str(),
                            p.kind.c_str());
    const std::string& k = p.kind;

    if (k == "boolean" || k == "atomic_bool") {
        ImGui::Checkbox("##b", &p.booleanVal);
    } else if (k == "int" || k == "long" || k == "short" || k == "byte"
               || k == "atomic_int" || k == "atomic_long" || k == "bignum"
               || k == "bigdec") {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##v", p.scalar, sizeof(p.scalar),
                          ImGuiInputTextFlags_CharsDecimal);
    } else if (k == "float" || k == "double") {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##v", p.scalar, sizeof(p.scalar),
                          ImGuiInputTextFlags_CharsDecimal);
    } else if (k == "char") {
        ImGui::SetNextItemWidth(60);
        ImGui::InputText("##c", p.scalar, 2);
    } else if (k == "string" || k == "file" || k == "charbuf"
               || k == "uuid" || k == "pattern" || k == "url" || k == "uri"
               || k == "boxed") {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##s", p.scalar, sizeof(p.scalar));
    } else if (k == "enum") {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##e", p.scalar[0] ? p.scalar : "")) {
            for (auto& v : p.enumValues)
                if (ImGui::Selectable(v.c_str(), v == p.scalar))
                    std::strncpy(p.scalar, v.c_str(), sizeof(p.scalar)-1);
            ImGui::EndCombo();
        }
    } else if (k == "date") {
        ImGui::SetNextItemWidth(-1);
        if (p.dateStr[0] == 0)
            std::strncpy(p.dateStr, "2026-01-01 00:00:00", sizeof(p.dateStr)-1);
        ImGui::InputTextWithHint("##d", "yyyy-MM-dd HH:mm:ss",
                                  p.dateStr, sizeof(p.dateStr));
    } else if (k == "class" || k == "reflect") {
        ImGui::SetNextItemWidth(-1);
        if (p.nestedClass[0] == 0)
            std::strncpy(p.nestedClass, "java.lang.Object", sizeof(p.nestedClass)-1);
        ImGui::InputTextWithHint("##cn", "FQCN",
                                  p.nestedClass, sizeof(p.nestedClass));
    } else if (k == "object") {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##oc", "FQCN (no-arg ctor)",
                                  p.nestedClass, sizeof(p.nestedClass));
    } else if (k == "map" || k == "properties") {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "entries (%zu)###kv", p.kvPairs.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* kTypeNames[] = {
                "string","int","long","double","bool","null"
            };
            for (size_t i = 0; i < p.kvPairs.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::SetNextItemWidth(150);
                ImGui::InputText("##k", p.kvPairs[i].key, sizeof(p.kvPairs[i].key));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                ImGui::Combo("##t", &p.kvPairs[i].valueType, kTypeNames, 6);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-40);
                if (p.kvPairs[i].valueType == 4) {
                    bool b = (std::strcmp(p.kvPairs[i].value, "true") == 0);
                    if (ImGui::Checkbox("##b", &b))
                        std::strncpy(p.kvPairs[i].value,
                                      b ? "true" : "false",
                                      sizeof(p.kvPairs[i].value)-1);
                } else if (p.kvPairs[i].valueType == 5) {
                    ImGui::TextDisabled("null");
                } else {
                    ImGui::InputText("##v", p.kvPairs[i].value,
                                      sizeof(p.kvPairs[i].value));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    p.kvPairs.erase(p.kvPairs.begin() + i);
                    ImGui::PopID(); break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Add")) p.kvPairs.push_back(KVPair{});
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) p.kvPairs.clear();
            ImGui::TreePop();
        }
    } else if (k == "list" || k == "set" || k == "queue" ||
               k == "iterable" || k == "optional" ||
               k == "object_array" || k == "array" ||
               k == "prim_array" || k == "byte_array") {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "items (%zu)###li", p.listItems.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* kTypes[] = {"string","int","long","double","bool"};
            ImGui::SetNextItemWidth(120);
            ImGui::Combo("##it", &p.listItemType, kTypes, 5);
            ImGui::SameLine(); ImGui::TextDisabled("(item type)");
            for (size_t i = 0; i < p.listItems.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::SetNextItemWidth(-40);
                char buf[512];
                std::strncpy(buf, p.listItems[i].c_str(), sizeof(buf)-1);
                buf[sizeof(buf)-1] = 0;
                if (ImGui::InputText("##v", buf, sizeof(buf)))
                    p.listItems[i] = buf;
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    p.listItems.erase(p.listItems.begin() + i);
                    ImGui::PopID(); break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Add")) p.listItems.push_back("");
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) p.listItems.clear();
            ImGui::TreePop();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.90f, 1.0f));
        ImGui::TextWrapped("JSON spec (advanced):");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##js", p.jsonSpec, sizeof(p.jsonSpec),
                                   ImVec2(-1, 90),
                                   ImGuiInputTextFlags_AllowTabInput);
    }
    ImGui::PopID();
}

void ClassBrowser::drawInstancePicker(const std::string& cls) {
    auto it = instances_.find(cls);
    if (it == instances_.end() || it->second.empty()) {
        if (ImGui::Button("Find Instances", ImVec2(180, 36))) findInstances(cls);
        return;
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Instances:");
    for (size_t i = 0; i < it->second.size(); ++i) {
        ImGui::PushID((int)i);
        char lbl[256];
        std::snprintf(lbl, sizeof(lbl), "#%zu %s", i,
                      it->second[i].className.c_str());
        bool sel = ((int)i == selectedInstance_);
        if (ImGui::Selectable(lbl, sel)) selectedInstance_ = (int)i;
        ImGui::PopID();
    }
}

} // namespace yamgg
