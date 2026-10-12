// ===========================================================================
// ClassBrowser.cpp — JV tab. Type-aware editors per kind.
// ===========================================================================
#include "ClassBrowser.h"
#include "JSConsole.h"
#include "../../Bridge/YamBridge.h"
#include "../Widgets/Notification.h"

#include "imgui.h"
#include "yam.hpp"

#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sstream>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {
std::string jesc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
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
} // anon

ClassBrowser& ClassBrowser::instance() { static ClassBrowser i; return i; }
ClassBrowser::ClassBrowser() { registerEvents(); }
ClassBrowser::~ClassBrowser() = default;

void ClassBrowser::sendCommand(const std::string& json) {
    YamBridge::instance().postCommand(json);
}

// ═══════════════════════════════════════════════════════════════════════════
// Event routing
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::registerEvents() {
    if (eventsRegistered_) return;
    eventsRegistered_ = true;

    // ── Chunk reassembly: when JS sends >128KB payload, bootstrap splits
    //    it into chunk_begin/data/end. The chunks module in yam_events.cpp
    //    reassembles, then calls the registered "full handler". We
    //    re-dispatch as a normal event so existing handlers fire.
    if (!chunksRegistered_) {
        chunksRegistered_ = true;
        yam::chunks::set_full_handler(
            [](const std::string& kind, const std::string& json) {
                auto parsed = yam::JsonValue::parse(json);
                if (!parsed) {
                    LOGI("CB: chunk parse FAILED kind=%s size=%zu",
                         kind.c_str(), json.size());
                    return;
                }
                LOGI("CB: chunk dispatched kind=%s size=%zu",
                     kind.c_str(), json.size());
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
        // Force drawTree to rebuild filtered_ on next frame
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
                        // Initialize editor from defaultSpec
                        if (auto* ds = e.get("defaultSpec")) {
                            std::string k = pi.kind;
                            if (k == "string" || k == "file") {
                                if (auto* v = ds->get("value"))
                                    std::strncpy(pi.scalar, v->as_str().c_str(),
                                                 sizeof(pi.scalar)-1);
                            } else if (k == "int" || k == "long" || k == "short"
                                    || k == "byte") {
                                if (auto* v = ds->get("value"))
                                    std::snprintf(pi.scalar, sizeof(pi.scalar),
                                                  "%lld", (long long)v->as_i64(0));
                            } else if (k == "float" || k == "double") {
                                if (auto* v = ds->get("value"))
                                    std::snprintf(pi.scalar, sizeof(pi.scalar),
                                                  "%g", v->as_f64(0));
                            } else if (k == "boolean") {
                                if (auto* v = ds->get("value"))
                                    pi.booleanVal = v->as_bool(false);
                            } else if (k == "char") {
                                if (auto* v = ds->get("value")) {
                                    std::string s = v->as_str("a");
                                    pi.scalar[0] = s.empty() ? 'a' : s[0];
                                    pi.scalar[1] = 0;
                                }
                            } else if (k == "enum") {
                                if (auto* v = ds->get("enumName"))
                                    std::strncpy(pi.scalar, v->as_str().c_str(),
                                                 sizeof(pi.scalar)-1);
                            } else {
                                std::string j = ds->stringify();
                                std::strncpy(pi.jsonSpec, j.c_str(),
                                             sizeof(pi.jsonSpec)-1);
                            }
                        }
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
                        if (auto* ds = e.get("defaultSpec")) {
                            std::string k = pi.kind;
                            if (k == "string") {
                                if (auto* v = ds->get("value"))
                                    std::strncpy(pi.scalar, v->as_str().c_str(),
                                                 sizeof(pi.scalar)-1);
                            } else if (k == "int" || k == "long" || k == "short"
                                    || k == "byte") {
                                if (auto* v = ds->get("value"))
                                    std::snprintf(pi.scalar, sizeof(pi.scalar),
                                                  "%lld", (long long)v->as_i64(0));
                            } else if (k == "float" || k == "double") {
                                if (auto* v = ds->get("value"))
                                    std::snprintf(pi.scalar, sizeof(pi.scalar),
                                                  "%g", v->as_f64(0));
                            } else if (k == "boolean") {
                                if (auto* v = ds->get("value"))
                                    pi.booleanVal = v->as_bool(false);
                            } else {
                                std::string j = ds->stringify();
                                std::strncpy(pi.jsonSpec, j.c_str(),
                                             sizeof(pi.jsonSpec)-1);
                            }
                        }
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
        LOGI("CB: class_detail %s methods=%zu fields=%zu ctors=%zu",
             cls.c_str(),
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
        LOGI("CB: members_search %s -> %zu matches",
             ev.get_str("query").c_str(), memberMatches_.size());
    });

    yam::events::on("call_result", [](const yam::Event& ev) {
        std::string result = ev.get_str("result");
        std::string err = ev.get_str("error");
        if (!err.empty()) JSConsole::instance().pushOutput("[call] ERR: " + err);
        else              JSConsole::instance().pushOutput("[call] " + result);
    });

    yam::events::on("trace_line", [](const yam::Event& ev) {
        std::string line = ev.get_str("line");
        if (!line.empty()) JSConsole::instance().pushOutput(line);
    });

    yam::events::on("trace_ok", [](const yam::Event& ev) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[trace] %s.%s (%d overloads)",
            ev.get_str("className").c_str(),
            ev.get_str("methodName").c_str(),
            (int)ev.get_i64("count", 0));
        JSConsole::instance().pushOutput(buf);
    });
    yam::events::on("trace_err", [](const yam::Event& ev) {
        JSConsole::instance().pushOutput("[trace] ERR: " + ev.get_str("error"));
    });

    yam::events::on("classes_error", [this](const yam::Event&) {
        std::lock_guard<std::mutex> lk(mu_);
        loading_ = false;
    });
    yam::events::on("class_detail_error", [this](const yam::Event&) {
        std::lock_guard<std::mutex> lk(mu_);
        loading_ = false;
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Command triggers
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::triggerLoadClasses() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!classes_.empty()) {
            // Already loaded — just invalidate filter cache.
            lastQuery_ = "\x01INVALIDATE\x01";
            return;
        }
    }
    loading_ = true;
    {
        std::lock_guard<std::mutex> lk(mu_);
        classes_.clear();
        filtered_.clear();
        lastQuery_.clear();
    }
    std::string cmd = R"({"action":"jv_enumerate","filter":""})";
    sendCommand(cmd);
}

void ClassBrowser::triggerLoadMethods(const std::string& cls) {
    loading_ = true;
    LOGI("CB: triggerLoadMethods %s", cls.c_str());
    std::string cmd = R"({"action":"jv_detail","className":")";
    cmd += jesc(cls); cmd += R"("})";
    sendCommand(cmd);
}

void ClassBrowser::triggerTrace(const std::string& cls, MethodInfo& m) {
    std::string cmd = R"({"action":"jv_trace","className":")";
    cmd += jesc(cls); cmd += R"(","methodName":")"; cmd += jesc(m.name);
    cmd += R"("})";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    m.tracing = true;
    tracedMethods_.insert(cls + "::" + m.name);
}

void ClassBrowser::triggerUntrace(const std::string& cls, MethodInfo& m) {
    std::string cmd = R"({"action":"jv_untrace","className":")";
    cmd += jesc(cls); cmd += R"(","methodName":")"; cmd += jesc(m.name);
    cmd += R"("})";
    sendCommand(cmd);
    std::lock_guard<std::mutex> lk(mu_);
    m.tracing = false;
    tracedMethods_.erase(cls + "::" + m.name);
}

std::string ClassBrowser::argsSpecFor(const std::vector<ParamInfo>& args) {
    std::string out;
    out.reserve(256);
    out += "[";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += ",";
        const ParamInfo& p = args[i];
        const std::string& k = p.kind;
        if (k == "int" || k == "long" || k == "short" || k == "byte") {
            out += R"({"kind":")"; out += k; out += R"(","value":)";
            out += (p.scalar[0] ? p.scalar : "0"); out += "}";
        } else if (k == "float" || k == "double") {
            out += R"({"kind":")"; out += k; out += R"(","value":)";
            out += (p.scalar[0] ? p.scalar : "0"); out += "}";
        } else if (k == "boolean") {
            out += R"({"kind":"boolean","value":)";
            out += (p.booleanVal ? "true" : "false"); out += "}";
        } else if (k == "char") {
            out += R"({"kind":"char","value":")";
            char ch = p.scalar[0] ? p.scalar[0] : 'a';
            char tmp[3] = { ch, 0, 0 };
            out += jesc(tmp); out += R"("})";
        } else if (k == "string" || k == "file" || k == "boxed") {
            out += R"({"kind":"string","value":")";
            out += jesc(p.scalar); out += R"("})";
        } else if (k == "enum") {
            out += R"({"kind":"enum","className":")";
            out += jesc(p.typeName);
            out += R"(","enumName":")";
            out += jesc(p.scalar);
            out += R"("})";
        } else if (k == "date") {
            out += R"({"kind":"date","value":")";
            out += jesc(p.dateStr[0] ? p.dateStr : "2026-01-01 00:00:00");
            out += R"("})";
        } else if (k == "class") {
            out += R"({"kind":"class","value":")";
            out += jesc(p.nestedClass[0] ? p.nestedClass : "java.lang.Object");
            out += R"("})";
        } else if (k == "object" && p.nestedClass[0]) {
            out += R"({"kind":"object","className":")";
            out += jesc(p.nestedClass);
            out += R"(","args":[]})";
        } else if (k == "date") {
        ImGui::SetNextItemWidth(-1);
        if (p.dateStr[0] == 0) std::strncpy(p.dateStr, "2026-01-01 00:00:00",
                                             sizeof(p.dateStr)-1);
        ImGui::InputTextWithHint("##d", "yyyy-MM-dd HH:mm:ss",
                                  p.dateStr, sizeof(p.dateStr));
    } else if (k == "class") {
        ImGui::SetNextItemWidth(-1);
        if (p.nestedClass[0] == 0) std::strncpy(p.nestedClass, "java.lang.Object",
                                                sizeof(p.nestedClass)-1);
        ImGui::InputTextWithHint("##cn", "fully.qualified.ClassName",
                                  p.nestedClass, sizeof(p.nestedClass));
    } else if (k == "object") {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##oc", "FQCN (constructed with no-arg)",
                                  p.nestedClass, sizeof(p.nestedClass));
        ImGui::TextDisabled("(uses no-arg ctor; use expr for complex)");
    } else if (k == "map" || k == "json") {
            // Build with auto-wrapped value types
            std::string cls = p.typeName;
            if (cls.empty() || cls == "java.util.Map" ||
                cls == "java.util.HashMap")
                cls = "java.util.HashMap";
            out += R"({"kind":"map","className":")";
            out += jesc(cls);
            out += R"(","entries":[)";
            for (size_t j = 0; j < p.kvPairs.size(); ++j) {
                if (j) out += ",";
                const KVPair& kv = p.kvPairs[j];
                out += R"({"k":{"kind":"string","value":")";
                out += jesc(kv.key);
                out += R"("},"v":)";
                switch (kv.valueType) {
                case 1: // int
                    out += R"({"kind":"int","value":)";
                    out += (kv.value[0] ? kv.value : "0"); out += "}";
                    break;
                case 2: // long
                    out += R"({"kind":"long","value":)";
                    out += (kv.value[0] ? kv.value : "0"); out += "}";
                    break;
                case 3: // double
                    out += R"({"kind":"double","value":)";
                    out += (kv.value[0] ? kv.value : "0"); out += "}";
                    break;
                case 4: // boolean
                    out += R"({"kind":"boolean","value":)";
                    out += (std::strcmp(kv.value, "true") == 0 ? "true" : "false");
                    out += "}";
                    break;
                case 5: // null
                    out += R"({"kind":"null"})";
                    break;
                default: // string
                    out += R"({"kind":"string","value":")";
                    out += jesc(kv.value);
                    out += R"("})";
                }
                out += "}";
            }
            out += "]}";
        } else if (k == "list" || k == "set" ||
                   k == "object_array" || k == "array" ||
                   k == "prim_array" || k == "byte_array") {
            std::string listKind = (k == "set") ? "set" : "list";
            std::string cls = p.typeName;
            if (cls.empty() || cls == "java.util.List" || cls == "java.util.Collection")
                cls = "java.util.ArrayList";
            if (k == "set" && cls == "java.util.Set") cls = "java.util.HashSet";
            out += R"({"kind":")"; out += listKind;
            out += R"(","className":")"; out += jesc(cls);
            out += R"(","items":[)";
            for (size_t j = 0; j < p.listItems.size(); ++j) {
                if (j) out += ",";
                switch (p.listItemType) {
                case 1:
                    out += R"({"kind":"int","value":)";
                    out += (p.listItems[j].empty() ? "0" : p.listItems[j].c_str());
                    out += "}";
                    break;
                case 2:
                    out += R"({"kind":"long","value":)";
                    out += (p.listItems[j].empty() ? "0" : p.listItems[j].c_str());
                    out += "}";
                    break;
                case 3:
                    out += R"({"kind":"double","value":)";
                    out += (p.listItems[j].empty() ? "0" : p.listItems[j].c_str());
                    out += "}";
                    break;
                case 4:
                    out += R"({"kind":"boolean","value":)";
                    out += (p.listItems[j] == "true" ? "true" : "false");
                    out += "}";
                    break;
                default:
                    out += R"({"kind":"string","value":")";
                    out += jesc(p.listItems[j]);
                    out += R"("})";
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

void ClassBrowser::triggerCall(const std::string& cls, MethodInfo& m) {
    std::string args = argsSpecFor(m.args);
    unsigned long long ih = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = instances_.find(cls);
        if (it != instances_.end() && selectedInstance_ >= 0
            && selectedInstance_ < (int)it->second.size())
            ih = it->second[selectedInstance_].handle;
    }
    std::string cmd = R"({"action":"jv_call","className":")";
    cmd += jesc(cls);
    cmd += R"(","methodName":")"; cmd += jesc(m.name);
    cmd += R"(","isStatic":)"; cmd += (m.isStatic ? "true" : "false");
    cmd += R"(,"instanceHandle":)"; cmd += std::to_string(ih);
    cmd += R"(,"argsSpec":)"; cmd += args;
    cmd += "}";
    sendCommand(cmd);
}

void ClassBrowser::triggerNewInstance(const std::string& cls, CtorInfo& c) {
    std::string args = argsSpecFor(c.args);
    std::string cmd = R"({"action":"jv_new","className":")";
    cmd += jesc(cls);
    cmd += R"(","argsSpec":)"; cmd += args;
    cmd += "}";
    sendCommand(cmd);
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
    cmd += R"(","wantMethod":)";
    cmd += (searchMethod_ ? "true" : "false");
    cmd += R"(,"wantField":)";
    cmd += (searchField_ ? "true" : "false");
    cmd += "}";
    LOGI("CB: triggerMemberSearch q=%s wantM=%d wantF=%d",
         q.c_str(), (int)searchMethod_, (int)searchField_);
    sendCommand(cmd);
}

// ═══════════════════════════════════════════════════════════════════════════
// Drawing
// ═══════════════════════════════════════════════════════════════════════════
void ClassBrowser::draw() {
    if (!autoLoadTried_) { autoLoadTried_ = true; triggerLoadClasses(); }
    drawSearchBar();
    ImGui::Separator();
    ImGui::BeginChild("##TreePanel", ImVec2(0, 0), true);
    drawTree();
    ImGui::EndChild();
}

void ClassBrowser::drawSearchBar() {
    ImGui::Spacing();
    ImGui::Checkbox("Class",  &searchClass_);
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
        triggerLoadClasses();
        if (searchMethod_ || searchField_) triggerMemberSearch();
    }
    if (loading_) { ImGui::SameLine(); ImGui::TextDisabled("..."); }
}

void ClassBrowser::drawTree() {
    std::lock_guard<std::mutex> lk(mu_);

    // ── Member matches (Method/Field search) ─────────────────────
    if (!memberMatches_.empty()) {
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "MEMBER MATCHES (%zu)",
                      memberMatches_.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.42f, 1.0f));
        ImGui::TextUnformatted(hdr);
        ImGui::PopStyleColor();
        ImGui::BeginChild("##memmatches", ImVec2(0, 220), true);
        for (size_t i = 0; i < memberMatches_.size(); ++i) {
            MemberMatch& m = memberMatches_[i];
            ImGui::PushID((int)i);
            char label[512];
            if (m.kind == "method") {
                std::snprintf(label, sizeof(label), "[M] %s::%s(%d) -> %s",
                              m.className.c_str(), m.name.c_str(),
                              m.argCount, m.ret.c_str());
            } else {
                std::snprintf(label, sizeof(label), "[F] %s::%s : %s",
                              m.className.c_str(), m.name.c_str(),
                              m.ret.c_str());
            }
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

    // ── Classes tree (filtered) ──────────────────────────────────
    std::string q = searchBuf_;
    if (q != lastQuery_) {
        lastQuery_ = q;
        filtered_.clear();
        filtered_.reserve(classes_.size());

        std::string qlow = q;
        for (auto& ch : qlow) ch = (char)std::tolower((unsigned char)ch);

        if (qlow.empty()) {
            filtered_ = classes_;
        } else {
            for (auto& c : classes_) {
                std::string cl = c;
                for (auto& ch : cl) ch = (char)std::tolower((unsigned char)ch);
                if (cl.find(qlow) != std::string::npos)
                    filtered_.push_back(c);
            }
        }
        LOGI("CB: filtered %zu / %zu (q=%s)", filtered_.size(),
             classes_.size(), q.c_str());
    }

    if (filtered_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (classes_.empty())
            ImGui::TextWrapped("Loading classes...");
        else
            ImGui::TextWrapped("No class match");
        ImGui::PopTextWrapPos();
        return;
    }
    std::string lastPkg;
    std::vector<std::string> members;
    auto flush = [&]() {
        if (!members.empty()) {
            drawPackageGroup(lastPkg, members); members.clear();
        }
    };
    for (auto& c : filtered_) {
        size_t dot = c.find_last_of('.');
        std::string pkg = (dot == std::string::npos)
                        ? "<default>" : c.substr(0, dot);
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
    // Member search box (filters methods + fields by substring, ci)
    ImGui::PushID((cls + "#ms").c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ms", "filter members (methods/fields)...",
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

    // ── Fields ────────────────────────────────────────────────────
    auto fit = fields_.find(cls);
    if (fit != fields_.end() && !fit->second.empty()) {
        size_t shown = 0;
        for (auto& f : fit->second) if (match(f.name)) shown++;
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Fields (%zu/%zu)###fld",
                      shown, fit->second.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            if (ImGui::BeginTable("##fields", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("name");
                ImGui::TableSetupColumn("type");
                ImGui::TableSetupColumn("value");
                ImGui::TableSetupColumn("static");
                ImGui::TableHeadersRow();
                for (auto& f : fit->second) {
                    if (!match(f.name)) continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", f.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", f.type.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", f.value.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", f.isStatic ? "yes" : "");
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }

    // ── Constructors ─────────────────────────────────────────────
    auto cit = ctors_.find(cls);
    if (cit != ctors_.end() && !cit->second.empty()) {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Constructors (%zu)###ctr",
                      cit->second.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (auto& c : cit->second) drawCtorNode(cls, c);
            ImGui::TreePop();
        }
    }

    // ── Methods ──────────────────────────────────────────────────
    auto mit = methods_.find(cls);
    if (mit != methods_.end() && !mit->second.empty()) {
        size_t shown = 0;
        for (auto& m : mit->second) if (match(m.name)) shown++;
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Methods (%zu/%zu)###mth",
                      shown, mit->second.size());
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen
                                    | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (auto& m : mit->second) {
                if (!match(m.name)) continue;
                drawMethodNode(cls, m);
            }
            ImGui::TreePop();
        }
    }

    if (methods_.find(cls) == methods_.end()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("loading members...");
        ImGui::PopTextWrapPos();
    }
}

void ClassBrowser::drawCtorNode(const std::string& cls, CtorInfo& c) {
    ImGui::PushID(&c);
    char label[512];
    std::snprintf(label, sizeof(label), "new %s(%zu)", cls.c_str(), c.args.size());
    if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (size_t i = 0; i < c.args.size(); ++i) drawParamWidget(c.args[i], (int)i);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.24f, 0.08f, 1.0f));
        if (ImGui::Button("NEW", ImVec2(110, 40))) triggerNewInstance(cls, c);
        ImGui::PopStyleColor(2);
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
    char label[2048];
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
        for (size_t i = 0; i < m.args.size(); ++i) drawParamWidget(m.args[i], (int)i);
        ImGui::Separator();
        if (!m.isStatic) drawInstancePicker(cls);
        if (m.tracing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.32f, 0.16f, 1.0f));
            if (ImGui::Button("STOP", ImVec2(110, 40))) triggerUntrace(cls, m);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
            if (ImGui::Button("TRACE", ImVec2(110, 40))) triggerTrace(cls, m);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.16f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.42f, 1.0f));
        if (ImGui::Button("CALL", ImVec2(110, 40))) triggerCall(cls, m);
        ImGui::PopStyleColor(2);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ClassBrowser::drawParamWidget(ParamInfo& p, int idx) {
    ImGui::PushID(idx);

    // ── Header ────────────────────────────────────────────────────
    if (!p.generic.empty())
        ImGui::TextWrapped("arg%d %s <%s>", idx, p.typeName.c_str(),
                            p.generic.c_str());
    else
        ImGui::TextWrapped("arg%d (%s) [%s]", idx, p.typeName.c_str(),
                            p.kind.c_str());

    const std::string& k = p.kind;

    if (k == "boolean") {
        ImGui::Checkbox("##b", &p.booleanVal);
    } else if (k == "int" || k == "long" || k == "short" || k == "byte") {
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
    } else if (k == "string" || k == "file") {
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
    } else if (k == "handle") {
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("##h", p.scalar, sizeof(p.scalar),
                          ImGuiInputTextFlags_CharsDecimal);
    } else if (k == "map" || k == "json") {
        // ── MAP / JSON: key-value table ─────────────────────────
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "entries (%zu)###kv",
                      p.kvPairs.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.82f, 0.90f, 1.0f));
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();
            static const char* kTypeNames[] = {
                "string", "int", "long", "double", "bool", "null"
            };
            for (size_t i = 0; i < p.kvPairs.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::SetNextItemWidth(150);
                ImGui::InputText("##k", p.kvPairs[i].key,
                                  sizeof(p.kvPairs[i].key));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                ImGui::Combo("##t", &p.kvPairs[i].valueType,
                              kTypeNames, 6);
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
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Add entry"))
                p.kvPairs.push_back(KVPair{});
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
                p.kvPairs.clear();
            ImGui::TreePop();
        } else {
            ImGui::PopStyleColor();
        }
    } else if (k == "list" || k == "set" ||
               k == "object_array" || k == "array" ||
               k == "prim_array" || k == "byte_array") {
        // ── LIST / SET / ARRAY: item rows ───────────────────────
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "items (%zu)###li",
                      p.listItems.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.82f, 0.90f, 1.0f));
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();
            static const char* kTypes[] = {
                "string", "int", "long", "double", "bool"
            };
            ImGui::SetNextItemWidth(120);
            ImGui::Combo("##it", &p.listItemType, kTypes, 5);
            ImGui::SameLine();
            ImGui::TextDisabled("(item type)");
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
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Add item"))
                p.listItems.push_back("");
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
                p.listItems.clear();
            ImGui::TreePop();
        } else {
            ImGui::PopStyleColor();
        }
    } else {
        // Fallback: raw JSON
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.72f, 0.90f, 1.0f));
        ImGui::TextWrapped("JSON spec (edit freely):");
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
    for (size_t i = 0; i < it->second.size(); ++i) {
        ImGui::PushID((int)i);
        char lbl[256];
        std::snprintf(lbl, sizeof(lbl), "#%zu %s", i,
                      it->second[i].className.c_str());
        bool sel = ((int)i == selectedInstance_);
        ImGui::PushTextWrapPos(0.0f);
        if (ImGui::Selectable(lbl, sel)) selectedInstance_ = (int)i;
        ImGui::PopTextWrapPos();
        if (sel && ImGui::BeginTable("##if", 2,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("field");
            ImGui::TableSetupColumn("value");
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
