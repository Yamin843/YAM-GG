#include "ClassBrowser.h"
#include "../../Bridge/YamBridge.h"

#include "imgui.h"

#include <android/log.h>
#include <cstring>
#include <sstream>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

ClassBrowser& ClassBrowser::instance() {
    static ClassBrowser inst;
    return inst;
}

ClassBrowser::ClassBrowser() = default;
ClassBrowser::~ClassBrowser() = default;

void ClassBrowser::draw() {
    ImGui::Spacing();
    ImGui::TextUnformatted("Class Browser");
    ImGui::Separator();
    ImGui::Spacing();

    // Filter
    char buf[256];
    std::strncpy(buf, filter_.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    ImGui::SetNextItemWidth(300);
    if (ImGui::InputText("Filter (package or class)", buf, sizeof(buf),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        filter_ = buf;
        triggerLoad();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        filter_ = buf;
        triggerLoad();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Two columns: classes (left), methods (right)
    float w = ImGui::GetContentRegionAvail().x;
    float colW = w * 0.4f;

    ImGui::BeginChild("##ClassesPane", ImVec2(colW, -60), true);
    drawClassList();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##MethodsPane", ImVec2(0, -60), true);
    drawMethodList();
    ImGui::EndChild();

    ImGui::Separator();
    if (loading_) {
        ImGui::TextDisabled("Loading...");
    } else {
        ImGui::TextDisabled("Enter a class name and press Load");
    }
}

void ClassBrowser::triggerLoad() {
    if (filter_.empty()) return;
    loading_ = true;
    classes_.clear();
    methods_.clear();
    selectedClass_.clear();
    selectedMethod_.clear();

    // Send JS eval to enumerate classes matching filter
    // The reply comes back via eval_result → onClassProbe
    std::string js =
        "(function(){"
        "try{"
        "var list=Java.enumerateLoadedClassesSync();"
        "var f='" + filter_ + "';"
        "var out=[];"
        "for(var i=0;i<list.length&&out.length<300;i++){"
        "if(list[i].indexOf(f)>=0)out.push(list[i]);}"
        "send({type:'classes_result',items:out});"
        "}catch(e){send({type:'classes_error',message:''+e});}"
        "})();";
    YamBridge::instance().evaluate(js);
    LOGI("ClassBrowser: requested %s", filter_.c_str());
}

void ClassBrowser::drawClassList() {
    ImGui::Text("Classes (%zu)", classes_.size());
    ImGui::Separator();
    for (auto& c : classes_) {
        bool selected = (selectedClass_ == c.name);
        if (ImGui::Selectable(c.name.c_str(), selected)) {
            selectedClass_ = c.name;
            selectedMethod_.clear();
            methods_.clear();
            // Trigger method probe
            std::string js =
                "(function(){"
                "try{"
                "var c=Java.use('" + c.name + "');"
                "var ms=c.class.getDeclaredMethods();"
                "var out=[];"
                "for(var i=0;i<ms.length;i++){"
                "var m=ms[i];"
                "try{m.setAccessible(true);}catch(e){}"
                "var sig='('+m.getParameterTypes().length+')';"
                "out.push({name:''+m.getName(),sig:sig,ret:''+m.getReturnType().getName()});}"
                "send({type:'methods_result',className:'" + c.name + "',items:out});"
                "}catch(e){send({type:'methods_error',message:''+e});}"
                "})();";
            YamBridge::instance().evaluate(js);
        }
    }
}

void ClassBrowser::drawMethodList() {
    if (selectedClass_.empty()) {
        ImGui::TextDisabled("Select a class");
        return;
    }
    ImGui::Text("Methods of %s (%zu)", selectedClass_.c_str(), methods_.size());
    ImGui::Separator();
    for (auto& m : methods_) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s %s%s", m.ret.c_str(), m.name.c_str(), m.sig.c_str());
        bool selected = (selectedMethod_ == buf);
        if (ImGui::Selectable(buf, selected)) {
            selectedMethod_ = buf;
        }
    }
}

void ClassBrowser::onClassesLoaded(const std::string&) {}
void ClassBrowser::onClassProbe(const std::string&, const std::string&) {}

} // namespace yamgg
