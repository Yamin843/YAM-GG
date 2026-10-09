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
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

JSConsole::JSConsole() {
    std::lock_guard<std::mutex> lk(mu_);
    output_.push_back("[YAM-GG] JS Console ready");
    output_.push_back("[YAM-GG] Load a .js file with 'fromSD' or type below.");
}
JSConsole::~JSConsole() = default;

JSConsole& JSConsole::instance() {
    static JSConsole inst;
    return inst;
}

void JSConsole::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    output_.push_back(line);
    if (output_.size() > 2000) output_.erase(output_.begin(), output_.begin() + 500);
}

void JSConsole::evaluate(const std::string& code) {
    if (code.empty()) return;
    YamBridge& bridge = YamBridge::instance();
    if (!bridge.isReady()) {
        pushOutput("[error] YAM bridge not ready");
        return;
    }
    pushOutput("> " + code);
    auto result = bridge.evaluate(code);
    if (!result.ok) {
        pushOutput("[error] " + result.error);
    } else if (!result.output.empty()) {
        std::istringstream ss(result.output);
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
    std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    ScriptEntry e;
    e.name = name;
    e.path = path;
    e.code = code;
    e.running = false;
    e.selected = true;
    e.id = nextScriptId_++;
    scripts_.push_back(e);

    pushOutput("[loaded] " + name + " (" + std::to_string(code.size()) + " bytes)");
    Notification::instance().push("Loaded: " + name);
}

void JSConsole::loadSelected() {
    YamBridge& bridge = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.selected) continue;
        if (s.running) continue;
        auto r = bridge.loadScript(s.name, s.code);
        if (r.ok) {
            s.running = true;
            pushOutput("[injected] " + s.name);
            Notification::instance().push("Injected: " + s.name);
        } else {
            pushOutput("[error] " + s.name + ": " + r.error);
        }
    }
}

void JSConsole::unloadSelected() {
    YamBridge& bridge = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.selected) continue;
        if (!s.running) continue;
        bridge.unloadScript(s.name);
        s.running = false;
        pushOutput("[unloaded] " + s.name);
    }
}

void JSConsole::unloadAll() {
    YamBridge& bridge = YamBridge::instance();
    for (auto& s : scripts_) {
        if (s.running) { bridge.unloadScript(s.name); s.running = false; }
    }
    pushOutput("[YAM-GG] all scripts unloaded");
}

void JSConsole::clearOutput() {
    std::lock_guard<std::mutex> lk(mu_);
    output_.clear();
}

int JSConsole::scriptCount() const { return (int)scripts_.size(); }
int JSConsole::runningCount() const {
    int n = 0;
    for (auto& s : scripts_) if (s.running) n++;
    return n;
}

void JSConsole::draw() {
    drawToolbar();
    ImGui::Separator();
    drawScriptsList();
    ImGui::Separator();
    drawCodeEditor();
    ImGui::Separator();
    drawOutput();
}

void JSConsole::drawToolbar() {
    ImGui::Spacing();

    if (ImGui::Button("fromSD", ImVec2(120, 40))) {
        FileBrowser::instance().setOnSelect(
            [](const std::string& path) {
                JSConsole::instance().loadScriptFromFile(path);
            });
        FileBrowser::instance().open("/storage/emulated/0/");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(120, 40))) loadSelected();
    ImGui::SameLine();
    if (ImGui::Button("Unload", ImVec2(120, 40))) unloadSelected();
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(120, 40))) clearOutput();

    ImGui::Spacing();
}

void JSConsole::drawScriptsList() {
    ImGui::Text("Scripts (%zu)", scripts_.size());
    ImGui::BeginChild("##ScriptsList", ImVec2(0, 150), true);
    for (size_t i = 0; i < scripts_.size(); ++i) {
        auto& s = scripts_[i];
        ImGui::PushID((int)i);

        ImGui::Checkbox("##sel", &s.selected);
        ImGui::SameLine();

        ImVec4 col = s.running
            ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
            : ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%s %s", s.running ? "[RUN]" : "[OFF]", s.name.c_str());
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetWindowWidth() - 110);
        if (ImGui::SmallButton("Remove")) {
            if (s.running) YamBridge::instance().unloadScript(s.name);
            scripts_.erase(scripts_.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void JSConsole::drawCodeEditor() {
    ImGui::Text("Editor");
    ImGui::BeginChild("##Editor", ImVec2(0, 200), true);
    ImGui::InputTextMultiline("##code",
        codeBuffer_, sizeof(codeBuffer_),
        ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput);
    ImGui::EndChild();

    if (ImGui::Button("Run Editor", ImVec2(150, 34))) {
        if (codeBuffer_[0]) evaluate(std::string(codeBuffer_));
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as script", ImVec2(150, 34))) {
        // Not implemented: needs writable path + dialog
        Notification::instance().push("Save: coming soon");
    }
}

void JSConsole::drawOutput() {
    ImGui::Text("Console Output");
    ImGui::BeginChild("##Output", ImVec2(0, -40), true,
                       ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& line : output_) {
            ImVec4 col(1.0f, 1.0f, 1.0f, 1.0f);
            if (line.rfind("[error]", 0) == 0) col = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
            else if (line.rfind("[warn]", 0) == 0) col = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
            else if (line.rfind("[injected]", 0) == 0) col = ImVec4(0.5f, 1.0f, 1.0f, 1.0f);
            else if (line.rfind("[loaded]", 0) == 0) col = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            else if (!line.empty() && line[0] == '>') col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);

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
