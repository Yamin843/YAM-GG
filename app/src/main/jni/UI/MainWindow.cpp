#include "MainWindow.h"
#include "Theme.h"
#include "Tabs/JSConsole.h"
#include "Tabs/ClassBrowser.h"
#include "Tabs/FileBrowser.h"
#include "Widgets/Notification.h"
#include "../Core/Runtime.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <android/log.h>
#include <cstring>

// ─── Vertical text helper (each char on own line) ───
static std::string verticalText(const char* s) {
    std::string out;
    for (const char* p = s; *p; ++p) {
        if (!out.empty()) out += '\n';
        out += *p;
    }
    return out;
}


#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

struct MainWindow::Impl {
    bool fullscreen{false};
    bool resetWindow{false};
    bool firstDraw{true};
    ImVec2 lastPos{0, 0};
    ImVec2 lastSize{0, 0};
    ImVec2 initialSize{0, 0};
    bool initializedSize{false};
    int currentTab{0};
    bool changeToJSTab{false};
    char titleBuf[128];
    float opacity{1.0f};
    bool showAboutDialog{false};
};

MainWindow::MainWindow() : impl_(new Impl()) {
    std::strncpy(impl_->titleBuf, "YAM-GG", sizeof(impl_->titleBuf) - 1);
}

MainWindow::~MainWindow() {
    delete impl_;
}

MainWindow& MainWindow::instance() {
    static MainWindow inst;
    return inst;
}

void MainWindow::draw() {
    if (!visible_) {
        Notification::instance().draw();
        return;
    }

    drawMainWindow();
    drawStatusBar();
    Notification::instance().draw();

    if (FileBrowser::instance().isOpen()) {
        FileBrowser::instance().draw();
    }

    if (impl_->showAboutDialog) {
        ImGui::OpenPopup("About YAM-GG");
        impl_->showAboutDialog = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About YAM-GG", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("YAM-GG");
        ImGui::Separator();
        ImGui::Text("Version 1.0.0");
        ImGui::Text("Independent ImGui menu with JS console.");
        ImGui::Text("Runs on its own GLSurfaceView render thread.");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MainWindow::drawMainWindow() {
    ImGuiIO& io = ImGui::GetIO();

    if (!impl_->initializedSize) {
        impl_->initialSize = ImVec2(io.DisplaySize.x * 0.85f,
                                     io.DisplaySize.y * 0.80f);
        impl_->initializedSize = true;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (impl_->fullscreen) {
        flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    }

    ImGui::SetNextWindowSizeConstraints(
            ImVec2(650.0f, 450.0f),
            ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (impl_->firstDraw) {
        ImGui::SetNextWindowPos(
                ImVec2((io.DisplaySize.x - impl_->initialSize.x) * 0.5f,
                       (io.DisplaySize.y - impl_->initialSize.y) * 0.5f),
                ImGuiCond_Always);
        ImGui::SetNextWindowSize(impl_->initialSize, ImGuiCond_Always);
        impl_->firstDraw = false;
    }

    if (impl_->resetWindow) {
        impl_->resetWindow = false;
        if (impl_->fullscreen) {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowPos(impl_->lastPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(impl_->lastSize, ImGuiCond_Always);
        }
    }

    ImGui::SetNextWindowBgAlpha(impl_->opacity);

    bool opened = ImGui::Begin(impl_->titleBuf, nullptr, flags);
    collapsed_ = !opened;

    if (!opened) {
        ImGui::End();
        return;
    }

    // MenuBar removed per user request

    // ─── Sidebar (horizontal labels, first tab = JV) ───
    const float sidebarW = 110.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(sidebarW, 0), true);
    {
        auto tabBtn = [&](const char* label, int id) {
            bool sel = (impl_->currentTab == id);
            ImVec2 btnSz(sidebarW - 12.0f, 60.0f);
            ImVec4 bg = sel ? ImVec4(0.45f, 0.35f, 0.00f, 1.0f)
                            : ImVec4(0.10f, 0.08f, 0.00f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(0.55f, 0.42f, 0.05f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,
                ImVec2(0.5f, 0.5f));
            if (ImGui::Button(label, btnSz)) impl_->currentTab = id;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        };

        tabBtn("JV", 0);
        tabBtn("JS",  1);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ─── Content ───
    ImGui::BeginChild("##ContentArea", ImVec2(0, 0), false);
    {
        if (impl_->currentTab == 0) {
            ClassBrowser::instance().draw();
        } else {
            JSConsole::instance().draw();
        }
    }
    ImGui::EndChild();

    // ─── Custom big resize grip (60x60 triangle, bottom-right) ───
    if (!impl_->fullscreen) {
        const float gripSz = 60.0f;
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 gripPos(winPos.x + winSize.x - gripSz, winPos.y + winSize.y - gripSz);

        ImGui::SetCursorScreenPos(gripPos);
        ImGui::InvisibleButton("##BigResizeGrip", ImVec2(gripSz, gripSz),
                               ImGuiButtonFlags_MouseButtonLeft);

        if (ImGui::IsItemActive()) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImVec2 ns(winSize.x + d.x, winSize.y + d.y);
            if (ns.x < 600.0f) ns.x = 600.0f;
            if (ns.y < 400.0f) ns.y = 400.0f;
            if (ns.x > io.DisplaySize.x) ns.x = io.DisplaySize.x;
            if (ns.y > io.DisplaySize.y) ns.y = io.DisplaySize.y;
            ImGui::SetWindowSize(ns);
        }

        // Draw the triangle on top
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a(gripPos.x + gripSz - 4, gripPos.y + gripSz - 4);
        ImVec2 b(gripPos.x + 4,           gripPos.y + gripSz - 4);
        ImVec2 c(gripPos.x + gripSz - 4, gripPos.y + 4);

        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();
        ImU32 col = active
            ? ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 0.80f, 1.00f))
            : (hovered
                ? ImGui::GetColorU32(ImVec4(1.00f, 0.95f, 0.30f, 1.00f))
                : ImGui::GetColorU32(ImVec4(1.00f, 0.80f, 0.10f, 1.00f)));

        dl->AddTriangleFilled(a, b, c, col);
        dl->AddTriangle(a, b, c, ImGui::GetColorU32(ImVec4(0.2f, 0.1f, 0.0f, 1.0f)), 2.0f);
    }

    ImGui::End();
}

void MainWindow::drawMenuBar() {
    // disabled per user request
}

void MainWindow::drawStatusBar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x,
                                    vp->WorkPos.y + vp->WorkSize.y - 30.0f));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 30.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.75f));
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::Text("YAM-GG | scripts: %d | running: %d",
                    JSConsole::instance().scriptCount(),
                    JSConsole::instance().runningCount());
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void MainWindow::openFileBrowser() {
    FileBrowser::instance().setOnSelect(
            [](const std::string& path) {
                JSConsole::instance().loadScriptFromFile(path);
            });
    FileBrowser::instance().open("/storage/emulated/0/");
}

void MainWindow::closeFileBrowser() {
    FileBrowser::instance().close();
}

bool MainWindow::fileBrowserOpen() const {
    return FileBrowser::instance().isOpen();
}

void MainWindow::notify(const std::string& msg, float duration) {
    Notification::instance().push(msg, duration);
}

} // namespace yamgg
