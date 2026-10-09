#include "FileBrowser.h"

#include "imgui.h"

#include <android/log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

FileBrowser& FileBrowser::instance() {
    static FileBrowser inst;
    return inst;
}

void FileBrowser::open(const std::string& initial) {
    currentPath_ = initial;
    open_ = true;
    refresh();
}

void FileBrowser::close() {
    open_ = false;
    rectValid_ = false;
}

void FileBrowser::navigateTo(const std::string& path) {
    currentPath_ = path;
    refresh();
}

void FileBrowser::goUp() {
    if (currentPath_.empty() || currentPath_ == "/") return;
    std::string p = currentPath_;
    if (p.back() == '/') p.pop_back();
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return;
    if (pos == 0) currentPath_ = "/";
    else currentPath_ = p.substr(0, pos);
    refresh();
}

void FileBrowser::refresh() {
    entries_.clear();
    error_.clear();

    DIR* dir = opendir(currentPath_.c_str());
    if (!dir) {
        error_ = "Cannot open: " + currentPath_;
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (!showHidden_ && !name.empty() && name[0] == '.') continue;

        Entry e;
        e.name = name;
        if (!currentPath_.empty() && currentPath_.back() == '/') {
            e.fullPath = currentPath_ + name;
        } else {
            e.fullPath = currentPath_ + "/" + name;
        }

        struct stat st;
        if (stat(e.fullPath.c_str(), &st) == 0) {
            e.isDir = S_ISDIR(st.st_mode);
            e.size = st.st_size;
        } else {
            e.isDir = (entry->d_type == DT_DIR);
            e.size = 0;
        }

        entries_.push_back(std::move(e));
    }
    closedir(dir);

    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) {
                  if (a.isDir != b.isDir) return a.isDir > b.isDir;
                  return a.name < b.name;
              });
}

void FileBrowser::draw() {
    if (!open_) return;

    ImGui::SetNextWindowSize(ImVec2(700, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool opened = ImGui::Begin("Select JS Script", &open_,
                                ImGuiWindowFlags_NoCollapse);
    if (!opened) {
        ImGui::End();
        rectValid_ = false;
        return;
    }

    // capture rect for hitTest()
    {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        rectX_ = wp.x; rectY_ = wp.y;
        rectW_ = ws.x; rectH_ = ws.y;
        rectValid_ = true;
    }

    // ─── Search filter ───
    {
        ImGui::SetNextItemWidth(-200);
        ImGui::InputTextWithHint("##filter", "filter...", filter_,
                                  sizeof(filter_));
        ImGui::SameLine();
        if (ImGui::Button("Up", ImVec2(60, 0))) {
            goUp();
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(80, 0))) {
            refresh();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Hidden", &showHidden_);
        if (ImGui::Button("Reload", ImVec2(80, 0))) {
            refresh();
        }
    }

    // ─── Breadcrumb path (built bottom-up, clickable) ───
    {
        ImGui::PushTextWrapPos(0.0f);
        std::string path = currentPath_;
        if (path.empty()) path = "/";
        // مسح الـ "/" النهائي إن وُجد
        if (path.size() > 1 && path.back() == '/') path.pop_back();

        std::vector<std::string> crumbs;
        size_t start = 0;
        if (path[0] == '/') { crumbs.push_back("/"); start = 1; }

        while (start < path.size()) {
            size_t slash = path.find('/', start);
            if (slash == std::string::npos) {
                crumbs.push_back(path.substr(start));
                break;
            }
            crumbs.push_back(path.substr(start, slash - start));
            start = slash + 1;
        }

        std::string cumulative;
        for (size_t i = 0; i < crumbs.size(); ++i) {
            if (i > 0 && !(i == 1 && crumbs[0] == "/")) {
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();
            }
            if (crumbs[i] == "/") {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.20f, 0.16f, 0.06f, 1.0f));
                if (ImGui::SmallButton("/")) navigateTo("/");
                ImGui::PopStyleColor(2);
                cumulative = "";
            } else {
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.20f, 0.16f, 0.06f, 1.0f));
                if (ImGui::SmallButton(crumbs[i].c_str())) {
                    if (cumulative.empty()) cumulative = "/";
                    else if (cumulative.back() != '/') cumulative += "/";
                    cumulative += crumbs[i];
                    navigateTo(cumulative);
                }
                ImGui::PopStyleColor(2);
                ImGui::PopID();
                if (cumulative.empty()) cumulative = "/";
                else if (cumulative.back() != '/') cumulative += "/";
                cumulative += crumbs[i];
            }
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    if (!error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", error_.c_str());
        ImGui::PopStyleColor();
    }

    if (!error_.empty()) {
        ImGui::End();
        return;
    }

    std::string f = filter_;
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);

    if (ImGui::BeginChild("##list", ImVec2(0, -60), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (auto& e : entries_) {
            if (!f.empty()) {
                std::string lower = e.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find(f) == std::string::npos) continue;
            }

            ImGui::PushID(e.fullPath.c_str());
            if (e.isDir) {
                // Folder: gold icon + name
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.95f, 0.78f, 0.20f, 1.0f));
                ImGui::TextUnformatted("▸");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 10);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.96f, 0.96f, 0.98f, 1.0f));
                ImGui::PushTextWrapPos(0.0f);
                bool clicked = ImGui::Selectable(e.name.c_str(), false);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                if (clicked) {
                    ImGui::PopID();
                    navigateTo(e.fullPath);
                    break;
                }
            } else {
                bool isJs = false;
                if (e.name.size() > 3) {
                    std::string ext = e.name.substr(e.name.size() - 3);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    isJs = (ext == ".js");
                }
                // File: small icon char
                if (isJs) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
                    ImGui::TextUnformatted("•");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
                    ImGui::TextUnformatted("·");
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine(0, 10);
                char disp[640];
                snprintf(disp, sizeof(disp), "%s", e.name.c_str());
                if (isJs) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.88f, 0.88f, 0.92f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.68f, 0.68f, 0.72f, 1.0f));
                }
                ImGui::PushTextWrapPos(0.0f);
                bool clicked = ImGui::Selectable(disp, false);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();

                // Size on the right
                ImGui::SameLine();
                char sizeBuf[64];
                snprintf(sizeBuf, sizeof(sizeBuf), "%lld B",
                         (long long)e.size);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
                ImGui::TextUnformatted(sizeBuf);
                ImGui::PopStyleColor();

                if (clicked && onSelect_) {
                    std::string path = e.fullPath;
                    ImGui::PopID();
                    onSelect_(path);
                    ImGui::EndChild();
                    ImGui::End();
                    return;
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        close();
    }

    ImGui::End();
}

bool FileBrowser::getRect(float& rx, float& ry, float& rw, float& rh) const {
    if (!open_ || !rectValid_) return false;
    if (rectW_ <= 0.0f || rectH_ <= 0.0f) return false;
    rx = rectX_; ry = rectY_; rw = rectW_; rh = rectH_;
    return true;
}

bool FileBrowser::hitTest(float x, float y) const {
    float rx, ry, rw, rh;
    if (!getRect(rx, ry, rw, rh)) return false;
    return x >= rx && x <= rx + rw &&
           y >= ry && y <= ry + rh;
}

} // namespace yamgg
