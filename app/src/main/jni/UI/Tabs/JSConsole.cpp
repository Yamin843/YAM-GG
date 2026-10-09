#include "JSConsole.h"
#include "FileBrowser.h"
#include "../Widgets/Notification.h"
#include "../../Bridge/YamBridge.h"
#include "../../Core/Runtime.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <android/log.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

JSConsole::JSConsole() {
    output_.push_back("[YAM-GG] JS Console ready");
    output_.push_back("[YAM-GG] Type JavaScript and press Enter to run.");
    output_.push_back("[YAM-GG] Use File > Load JS File to inject scripts from sdcard.");
    historyPos_ = -1;
}

JSConsole::~JSConsole() = default;

JSConsole& JSConsole::instance() {
    static JSConsole inst;
    return inst;
}

void JSConsole::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    output_.push_back(line);
    if (output_.size() > 4000) {
        output_.erase(output_.begin(), output_.begin() + 1000);
    }
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
        while (std::getline(ss, line)) {
            pushOutput(line);
        }
    }
}

void JSConsole::loadScriptFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        pushOutput("[error] cannot open: " + path);
        Notification::instance().push("Failed to open: " + path);
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string code = ss.str();

    size_t slash = path.find_last_of('/');
    std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    Script s;
    s.name = name;
    s.path = path;
    s.code = code;
    s.running = false;
    s.isFile = true;
    s.id = nextScriptId_++;
    scripts_.push_back(s);

    pushOutput("[loaded] " + name + " (" + std::to_string(code.size()) + " bytes)");
    Notification::instance().push("Loaded: " + name);

    YamBridge& bridge = YamBridge::instance();
    auto r = bridge.loadScript(name, code);
    if (r.ok) {
        scripts_.back().running = true;
        pushOutput("[injected] " + name);
        Notification::instance().push("Injected: " + name);
    } else {
        pushOutput("[error] injection failed: " + r.error);
        Notification::instance().push("Injection failed: " + r.error);
    }
}

void JSConsole::reloadScripts() {
    pushOutput("[YAM-GG] reloading all scripts...");
    YamBridge& bridge = YamBridge::instance();
    for (auto& s : scripts_) {
        if (!s.running) continue;
        bridge.unloadScript(s.name);
        auto r = bridge.loadScript(s.name, s.code);
        s.running = r.ok;
        if (!r.ok) pushOutput("[error] reload failed: " + s.name + ": " + r.error);
    }
    pushOutput("[YAM-GG] reload complete");
}

void JSConsole::unloadAll() {
    YamBridge& bridge = YamBridge::instance();
    for (auto& s : scripts_) {
        if (s.running) {
            bridge.unloadScript(s.name);
            s.running = false;
        }
    }
    pushOutput("[YAM-GG] all scripts unloaded");
    Notification::instance().push("All scripts unloaded");
}

void JSConsole::unloadScript(int index) {
    if (index < 0 || index >= (int)scripts_.size()) return;
    YamBridge& bridge = YamBridge::instance();
    auto& s = scripts_[index];
    if (s.running) {
        bridge.unloadScript(s.name);
        s.running = false;
        pushOutput("[unloaded] " + s.name);
        Notification::instance().push("Unloaded: " + s.name);
    }
}

int JSConsole::scriptCount() const {
    return (int)scripts_.size();
}

int JSConsole::runningCount() const {
    int n = 0;
    for (auto& s : scripts_) if (s.running) n++;
    return n;
}

void JSConsole::addHistory(const std::string& cmd) {
    if (cmd.empty()) return;
    if (!history_.empty() && history_.back() == cmd) return;
    history_.push_back(cmd);
    if (history_.size() > 200) history_.erase(history_.begin());
    historyPos_ = -1;
}

void JSConsole::draw() {
    ImGui::Spacing();

    float buttonWidth = 140.0f;
    float totalAvail = ImGui::GetContentRegionAvail().x;
    float halfWidth = (totalAvail - 20.0f) * 0.5f;
    if (halfWidth < buttonWidth) halfWidth = buttonWidth;

    if (ImGui::Button("Load JS File", ImVec2(halfWidth, 45))) {
        FileBrowser::instance().setOnSelect(
                [](const std::string& path) {
                    JSConsole::instance().loadScriptFromFile(path);
                });
        FileBrowser::instance().open("/storage/emulated/0/");
    }

    ImGui::SameLine();

    if (ImGui::Button("Clear Console", ImVec2(halfWidth, 45))) {
        std::lock_guard<std::mutex> lk(mu_);
        output_.clear();
        output_.push_back("[YAM-GG] console cleared");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawOutput();

    ImGui::Separator();
    drawInput();
}

void JSConsole::drawOutput() {
    float footerHeight = ImGui::GetFrameHeightWithSpacing() + 8.0f;
    if (ImGui::BeginChild("##js_output", ImVec2(0, -footerHeight), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& line : output_) {
            ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
            if (line.size() >= 7 && line.compare(0, 7, "[error]") == 0) {
                color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
            } else if (line.size() >= 6 && line.compare(0, 6, "[warn]") == 0) {
                color = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
            } else if (line.size() >= 8 && line.compare(0, 8, "[loaded]") == 0) {
                color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            } else if (line.size() >= 10 && line.compare(0, 10, "[injected]") == 0) {
                color = ImVec4(0.5f, 1.0f, 1.0f, 1.0f);
            } else if (line.size() >= 10 && line.compare(0, 10, "[unloaded]") == 0) {
                color = ImVec4(0.9f, 0.7f, 0.9f, 1.0f);
            } else if (!line.empty() && line[0] == '>') {
                color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
            } else if (line.size() >= 8 && line.compare(0, 8, "[YAM-GG]") == 0) {
                color = ImVec4(0.75f, 0.75f, 1.0f, 1.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (wrapText_) {
                ImGui::TextWrapped("%s", line.c_str());
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
            ImGui::PopStyleColor();
        }

        if (autoscroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::Checkbox("Autoscroll", &autoscroll_);
    ImGui::SameLine();
    ImGui::Checkbox("Wrap", &wrapText_);
}

void JSConsole::drawInput() {
    static char buf[4096] = {0};

    ImGui::PushItemWidth(-140.0f);
    bool enterPressed = ImGui::InputText("##js_input", buf, sizeof(buf),
                                          ImGuiInputTextFlags_EnterReturnsTrue,
                                          nullptr, nullptr);

    if (ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !history_.empty()) {
            if (historyPos_ == -1) historyPos_ = (int)history_.size() - 1;
            else if (historyPos_ > 0) historyPos_--;
            strncpy(buf, history_[historyPos_].c_str(), sizeof(buf) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !history_.empty()) {
            if (historyPos_ != -1 && historyPos_ < (int)history_.size() - 1) {
                historyPos_++;
                strncpy(buf, history_[historyPos_].c_str(), sizeof(buf) - 1);
            } else {
                historyPos_ = -1;
                buf[0] = 0;
            }
        }
    }

    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ImGui::Button("Run", ImVec2(120, 0)) || enterPressed) {
        std::string cmd = buf;
        buf[0] = 0;
        if (!cmd.empty()) {
            addHistory(cmd);
            evaluate(cmd);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(120, 0))) {
        std::lock_guard<std::mutex> lk(mu_);
        output_.clear();
    }
}

void JSConsole::drawScriptsTab() {
    ImGui::Spacing();
    ImGui::Text("Loaded Scripts");
    ImGui::Separator();

    if (scripts_.empty()) {
        ImGui::TextDisabled("No scripts loaded yet.");
        ImGui::Spacing();
        if (ImGui::Button("Open File Browser", ImVec2(220, 45))) {
            FileBrowser::instance().setOnSelect(
                    [](const std::string& path) {
                        JSConsole::instance().loadScriptFromFile(path);
                    });
            FileBrowser::instance().open("/storage/emulated/0/");
        }
        return;
    }

    if (ImGui::BeginChild("##scripts_list", ImVec2(0, -80), true)) {
        for (int i = 0; i < (int)scripts_.size(); i++) {
            auto& s = scripts_[i];
            ImGui::PushID(i);

            if (s.running) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            }
            ImGui::Text("%s [%s]", s.name.c_str(), s.running ? "RUNNING" : "STOPPED");
            ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetWindowWidth() - 300);
            if (s.running) {
                if (ImGui::Button("Unload", ImVec2(90, 0))) {
                    unloadScript(i);
                }
            } else {
                if (ImGui::Button("Inject", ImVec2(90, 0))) {
                    YamBridge& bridge = YamBridge::instance();
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

            ImGui::SameLine();
            if (ImGui::Button("Reload", ImVec2(90, 0))) {
                YamBridge& bridge = YamBridge::instance();
                bridge.unloadScript(s.name);
                auto r = bridge.loadScript(s.name, s.code);
                s.running = r.ok;
                pushOutput(r.ok ? "[reloaded] " + s.name : "[error] " + r.error);
            }

            ImGui::SameLine();
            if (ImGui::Button("Remove", ImVec2(90, 0))) {
                if (s.running) {
                    YamBridge::instance().unloadScript(s.name);
                }
                scripts_.erase(scripts_.begin() + i);
                ImGui::PopID();
                break;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Reload All", ImVec2(200, 0))) {
        reloadScripts();
    }
    ImGui::SameLine();
    if (ImGui::Button("Unload All", ImVec2(200, 0))) {
        unloadAll();
    }
}

} // namespace yamgg
