#include "JSConsole.h"
#include "FileBrowser.h"
#include "../Widgets/Notification.h"
#include "../../Bridge/YamBridge.h"

#include "imgui.h"
#include <android/log.h>
#include <cstring>
#include <fstream>
#include <sstream>

namespace yamgg {

JSConsole::JSConsole() = default;
JSConsole::~JSConsole() = default;
JSConsole& JSConsole::instance() { static JSConsole i; return i; }

void JSConsole::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    output_.push_back(line);
    if (output_.size() > 3000) output_.erase(output_.begin(), output_.begin() + 800);
}

void JSConsole::evaluate(const std::string& code) {
    if (code.empty()) return;
    YamBridge& b = YamBridge::instance();
    if (!b.isReady()) { pushOutput("[error] bridge not ready"); return; }
    pushOutput("> " + code);
    auto r = b.evaluate(code);
    if (!r.ok) pushOutput("[error] " + r.error);
    else if (!r.output.empty()) {
        std::istringstream ss(r.output);
        std::string line;
        while (std::getline(ss, line)) pushOutput(line);
    }
}

void JSConsole::loadScriptFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { pushOutput("[error] cannot open: " + path); return; }
    std::stringstream ss; ss << f.rdbuf();
    std::string code = ss.str();
    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    ScriptEntry e;
    e.name = name; e.path = path; e.code = code;
    e.selected = true; e.id = nextScriptId_++;
    scripts_.push_back(e);
    pushOutput("[loaded] " + name);
}

void JSConsole::loadSelected() {
    YamBridge& b = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.selected || s.running) continue;
        auto r = b.loadScript(s.name, s.code);
        if (r.ok) { s.running = true; pushOutput("[injected] " + s.name); }
        else pushOutput("[error] " + s.name + ": " + r.error);
    }
}

void JSConsole::unloadSelected() {
    YamBridge& b = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.selected || !s.running) continue;
        b.unloadScript(s.name); s.running = false;
        pushOutput("[unloaded] " + s.name);
    }
}

void JSConsole::unloadAll() {
    YamBridge& b = YamBridge::instance();
    for (auto& s : scripts_) if (s.running) { b.unloadScript(s.name); s.running = false; }
}

void JSConsole::clearOutput() { std::lock_guard<std::mutex> lk(mu_); output_.clear(); }
int JSConsole::scriptCount() const { return (int)scripts_.size(); }
int JSConsole::runningCount() const { int n = 0; for (auto& s : scripts_) if (s.running) n++; return n; }

void JSConsole::draw() {
    // Sub-tabs at top
    if (ImGui::BeginTabBar("##JSSubTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("LOG")) {
            activeSubTab_ = 0;
            drawLogTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            activeSubTab_ = 1;
            drawConsoleTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void JSConsole::drawLogTab() {
    ImGui::Spacing();
    ImGui::BeginChild("##LogArea", ImVec2(0, -50), true,
                       ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(mu_);
        ImGui::PushTextWrapPos(0.0f);
        for (auto& line : output_) {
            ImVec4 col(1, 1, 1, 1);
            if (line.rfind("[error]", 0) == 0) col = ImVec4(1, 0.45f, 0.45f, 1);
            else if (line.rfind("[warn]", 0) == 0) col = ImVec4(1, 0.85f, 0.3f, 1);
            else if (!line.empty() && line[0] == '>') col = ImVec4(1, 0.85f, 0.2f, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    if (ImGui::Button("Clear Log", ImVec2(180, 40))) clearOutput();
}

void JSConsole::drawConsoleTab() {
    // Toolbar
    ImGui::Spacing();
    const ImVec2 bsz(120, 42);
    if (ImGui::Button("Load", bsz)) loadSelected();
    ImGui::SameLine();
    if (ImGui::Button("Unload", bsz)) unloadSelected();
    ImGui::SameLine();
    if (ImGui::Button("fromSD", bsz)) {
        FileBrowser::instance().setOnSelect(
            [](const std::string& p) { JSConsole::instance().loadScriptFromFile(p); });
        FileBrowser::instance().open("/storage/emulated/0/");
    }
    ImGui::Spacing();
    ImGui::Separator();

    // Editor
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("Script");
    ImGui::PopTextWrapPos();
    ImGui::InputTextMultiline("##code", codeBuffer_, sizeof(codeBuffer_),
                              ImVec2(-1, 140), ImGuiInputTextFlags_AllowTabInput);
    if (ImGui::Button("Run", ImVec2(120, 40)))
        if (codeBuffer_[0]) evaluate(std::string(codeBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(120, 40))) codeBuffer_[0] = 0;

    ImGui::Separator();

    // Scripts list
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("Scripts: %d  |  Running: %d", scriptCount(), runningCount());
    ImGui::PopTextWrapPos();
    ImGui::BeginChild("##ScriptsList", ImVec2(0, 0), true);
    for (size_t i = 0; i < scripts_.size(); ++i) {
        auto& s = scripts_[i];
        ImGui::PushID((int)i);
        ImGui::Checkbox("##sel", &s.selected);
        ImGui::SameLine();
        ImVec4 col = s.running ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s %s", s.running ? "[RUN]" : "[OFF]", s.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetWindowWidth() - 110);
        if (ImGui::SmallButton("Remove")) {
            if (s.running) YamBridge::instance().unloadScript(s.name);
            scripts_.erase(scripts_.begin() + i);
            ImGui::PopID(); break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

} // namespace yamgg
