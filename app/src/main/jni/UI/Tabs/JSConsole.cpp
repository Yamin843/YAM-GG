#include "JSConsole.h"
#include "FileBrowser.h"
#include "../Widgets/Notification.h"
#include "../../Bridge/YamBridge.h"

#include "imgui.h"
#include <android/log.h>
#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>

namespace yamgg {

static ImVec2 autoBtn(const char* label, float padX = 28.0f, float padY = 16.0f) {
    ImVec2 ts = ImGui::CalcTextSize(label);
    return ImVec2(ts.x + padX, ts.y + padY);
}

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
    if (path.empty()) { pushOutput("[error] empty path"); return; }

    // validate .js extension
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        pushOutput("[error] not a .js file: " + path);
        return;
    }
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext != "js") {
        pushOutput("[error] not a .js file: " + path);
        return;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) { pushOutput("[error] cannot open: " + path); return; }
    std::stringstream ss; ss << f.rdbuf();
    std::string code = ss.str();
    if (code.empty()) {
        pushOutput("[error] empty script: " + path);
        return;
    }

    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    // إذا كان موجوداً، حدّث الكود
    for (auto& s : scripts_) {
        if (s.name == name) {
            s.code = code;
            s.path = path;
            s.selected = true;
            pushOutput("[updated] " + name +
                       " (" + std::to_string(code.size()) + " bytes)");
            return;
        }
    }

    ScriptEntry e;
    e.name = name; e.path = path; e.code = code;
    e.selected = true; e.id = nextScriptId_++;
    scripts_.push_back(e);
    pushOutput("[loaded] " + name +
               " (" + std::to_string(code.size()) + " bytes)");
}

void JSConsole::loadSelected() {
    YamBridge& b = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.selected || s.running) continue;
        // Use sync variant with 5s timeout — we only mark as "running"
        // if the JS side confirmed success.
        auto r = b.loadScriptSync(s.name, s.code, 5000);
        if (r.ok) {
            s.running = true;
            pushOutput("[injected] " + s.name);
        } else {
            pushOutput("[error] " + s.name + ": " +
                       (r.error.empty() ? "unknown" : r.error));
        }
    }
}

void JSConsole::unloadSelected() {
    YamBridge& b = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.selected || !s.running) continue;
        bool ok = b.unloadScript(s.name);
        s.running = false;
        pushOutput(ok ? ("[unloaded] " + s.name)
                      : ("[warn] " + s.name + ": unload may be partial"));
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
    if (ImGui::BeginTabBar("##JSSubTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Console")) {
            activeSubTab_ = 0;
            drawConsoleTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            activeSubTab_ = 1;
            drawLogTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void JSConsole::drawLogTab() {
    ImGui::Spacing();
    ImGui::BeginChild("##LogArea", ImVec2(0, -60), true,
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

    ImVec2 bs = autoBtn("Clear Log");
    if (ImGui::Button("Clear Log", bs)) clearOutput();
}

void JSConsole::drawConsoleTab() {
    ImGui::Spacing();
    {
        ImVec2 bs = autoBtn("fromSD");
        if (ImGui::Button("fromSD", bs)) {
            FileBrowser::instance().setOnSelect(
                [](const std::string& p) { JSConsole::instance().loadScriptFromFile(p); });
            FileBrowser::instance().open("/storage/emulated/0/");
        }
        ImGui::SameLine();
        bs = autoBtn("Load");
        if (ImGui::Button("Load", bs)) loadSelected();
        ImGui::SameLine();
        bs = autoBtn("Unload");
        if (ImGui::Button("Unload", bs)) unloadSelected();
        ImGui::SameLine();
        bs = autoBtn("Paste");
        if (ImGui::Button("Paste", bs)) {
            const char* cb = ImGui::GetClipboardText();
            if (cb && *cb) {
                size_t cur = std::strlen(codeBuffer_);
                size_t cap = sizeof(codeBuffer_) - 1;
                if (cur < cap) {
                    size_t room = cap - cur;
                    size_t n = std::strlen(cb);
                    size_t take = (n < room) ? n : room;
                    std::memcpy(codeBuffer_ + cur, cb, take);
                    codeBuffer_[cur + take] = 0;
                }
            }
        }
    }
    ImGui::Separator();

    // Editor
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("Script editor");
    ImGui::PopTextWrapPos();

    ImGui::InputTextMultiline("##code", codeBuffer_, sizeof(codeBuffer_),
                              ImVec2(-1, 180), ImGuiInputTextFlags_AllowTabInput);

    {
        ImVec2 bs = autoBtn("Run");
        if (ImGui::Button("Run", bs))
            if (codeBuffer_[0]) evaluate(std::string(codeBuffer_));
        ImGui::SameLine();
        bs = autoBtn("Clear");
        if (ImGui::Button("Clear", bs)) codeBuffer_[0] = 0;
    }

    ImGui::Separator();

    // Scripts list with checkboxes
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("Scripts: %d  |  Running: %d", scriptCount(), runningCount());
    ImGui::PopTextWrapPos();
    ImGui::BeginChild("##ScriptsList", ImVec2(0, 0), true);
    if (scripts_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("(use fromSD to load a file)");
        ImGui::PopTextWrapPos();
    }
    for (size_t i = 0; i < scripts_.size(); ++i) {
        auto& s = scripts_[i];
        ImGui::PushID((int)i);
        ImGui::Checkbox("##sel", &s.selected);
        ImGui::SameLine();
        ImVec4 col = s.running ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s %s", s.running ? "[RUN]" : "[OFF]", s.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        if (ImGui::SmallButton("X")) {
            if (s.running) YamBridge::instance().unloadScript(s.name);
            scripts_.erase(scripts_.begin() + i);
            ImGui::PopID(); break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

} // namespace yamgg
