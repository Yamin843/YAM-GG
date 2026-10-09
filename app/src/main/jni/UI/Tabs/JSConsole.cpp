#include "JSConsole.h"
#include "FileBrowser.h"
#include "../Widgets/Notification.h"
#include "../../Bridge/YamBridge.h"

#include "imgui.h"
#include <android/log.h>
#include <cstring>
#include <fstream>
#include <sstream>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

JSConsole::JSConsole() {
    std::lock_guard<std::mutex> lk(mu_);
    output_.push_back("[YAM-GG] JS Console ready.");
    output_.push_back("[YAM-GG] Use fromSD to load a script, or type below.");
}
JSConsole::~JSConsole() = default;
JSConsole& JSConsole::instance() { static JSConsole i; return i; }

void JSConsole::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    output_.push_back(line);
    if (output_.size() > 2000) output_.erase(output_.begin(), output_.begin() + 500);
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
    e.running = false; e.selected = true; e.id = nextScriptId_++;
    scripts_.push_back(e);
    pushOutput("[loaded] " + name + " (" + std::to_string(code.size()) + " bytes)");
    Notification::instance().push("Loaded: " + name);
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
    pushOutput("[YAM-GG] all scripts unloaded");
}

void JSConsole::clearOutput() {
    std::lock_guard<std::mutex> lk(mu_);
    output_.clear();
}

int JSConsole::scriptCount() const { return (int)scripts_.size(); }
int JSConsole::runningCount() const { int n = 0; for (auto& s : scripts_) if (s.running) n++; return n; }

void JSConsole::draw() {
    drawToolbar();
    ImGui::Separator();
    drawScriptsList();
    ImGui::Separator();
    drawEditor();
    ImGui::Separator();
    drawOutput();
}

void JSConsole::drawToolbar() {
    ImGui::Spacing();
    const ImVec2 bsz(130, 44);
    if (ImGui::Button("fromSD", bsz)) {
        FileBrowser::instance().setOnSelect(
            [](const std::string& p) { JSConsole::instance().loadScriptFromFile(p); });
        FileBrowser::instance().open("/storage/emulated/0/");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", bsz)) loadSelected();
    ImGui::SameLine();
    if (ImGui::Button("Unload", bsz)) unloadSelected();
    ImGui::SameLine();
    if (ImGui::Button("Clear", bsz)) clearOutput();
    ImGui::Spacing();
}

void JSConsole::drawScriptsList() {
    ImGui::Text("Scripts: %d  |  Running: %d", scriptCount(), runningCount());
    ImGui::BeginChild("##ScriptsList", ImVec2(0, 130), true);
    for (size_t i = 0; i < scripts_.size(); ++i) {
        auto& s = scripts_[i];
        ImGui::PushID((int)i);
        ImGui::Checkbox("##sel", &s.selected);
        ImGui::SameLine();
        ImVec4 col = s.running ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%s  %s", s.running ? "[RUN]" : "[OFF]", s.name.c_str());
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

void JSConsole::drawEditor() {
    ImGui::Text("Editor");
    ImGui::BeginChild("##Editor", ImVec2(0, 180), true);
    ImGui::InputTextMultiline("##code", codeBuffer_, sizeof(codeBuffer_),
                              ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput);
    ImGui::EndChild();
    if (ImGui::Button("Run", ImVec2(140, 40)))
        if (codeBuffer_[0]) evaluate(std::string(codeBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Clear Editor", ImVec2(140, 40)))
        codeBuffer_[0] = 0;
}

void JSConsole::drawOutput() {
    ImGui::Text("Console");
    ImGui::BeginChild("##Output", ImVec2(0, -44), true,
                       ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& line : output_) {
            ImVec4 col(1, 1, 1, 1);
            if (line.rfind("[error]", 0) == 0) col = ImVec4(1, 0.45f, 0.45f, 1);
            else if (line.rfind("[warn]", 0) == 0) col = ImVec4(1, 0.85f, 0.3f, 1);
            else if (line.rfind("[injected]", 0) == 0) col = ImVec4(0.5f, 1, 1, 1);
            else if (!line.empty() && line[0] == '>') col = ImVec4(1, 0.85f, 0.2f, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    if (ImGui::Button("Clear Console", ImVec2(180, 34))) clearOutput();
}

} // namespace yamgg
