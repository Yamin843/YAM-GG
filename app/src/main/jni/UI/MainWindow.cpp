
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
#include <cmath>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

struct MainWindow::Impl {
    bool   fullscreen{false};
    bool   resetWindow{false};
    bool   firstDraw{true};
    ImVec2 lastPos{0, 0};
    ImVec2 lastSize{0, 0};
    ImVec2 initialSize{0, 0};
    bool   initializedSize{false};
    int    currentTab{0};
    char   titleBuf[128];
    float  opacity{1.0f};

    // minimize
    bool   minimized{false};
    ImVec2 minimizedPos{40.0f, 120.0f};
    ImVec2 lastFullPos{0, 0};
    ImVec2 lastFullSize{0, 0};

    // window drag
    bool   windowDragging{false};
    ImVec2 dragStartMouse{0, 0};
    ImVec2 dragStartWindow{0, 0};
};

MainWindow::MainWindow() : impl_(new Impl()) {
    std::strncpy(impl_->titleBuf, "YAM-GG", sizeof(impl_->titleBuf) - 1);
    impl_->titleBuf[sizeof(impl_->titleBuf) - 1] = 0;
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
        // reset drag state so a subsequent re-show does not "jump"
        impl_->windowDragging = false;
        Notification::instance().draw();
        return;
    }

    if (impl_->minimized) {
        impl_->windowDragging = false;
        drawMinimized();
        Notification::instance().draw();
        return;
    }

    drawMainWindow();
    drawStatusBar();
    Notification::instance().draw();

    if (FileBrowser::instance().isOpen()) {
        FileBrowser::instance().draw();
    }
}

void MainWindow::drawMainWindow() {
    ImGuiIO& io = ImGui::GetIO();

    if (!impl_->initializedSize) {
        impl_->initialSize = ImVec2(io.DisplaySize.x * 0.85f,
                                     io.DisplaySize.y * 0.80f);
        impl_->initializedSize = true;
    }

    const float kMinW = 420.0f;
    const float kMinH = 320.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    if (impl_->fullscreen) {
        flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    }

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kMinW, kMinH),
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

    // ─── YG minimize button (top-right, inside window) ───
    {
        ImVec2 wpos  = ImGui::GetWindowPos();
        ImVec2 wsize = ImGui::GetWindowSize();
        const float sz = 40.0f;
        const float mg = 10.0f;

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.65f, 0.00f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.80f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.90f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.02f, 0.02f, 0.02f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sz * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);

        ImGui::SetCursorScreenPos(ImVec2(wpos.x + wsize.x - sz - mg,
                                          wpos.y + mg));
        ImGui::PushID("YG_btn");
        if (ImGui::Button("YG", ImVec2(sz, sz))) {
            impl_->lastFullPos  = wpos;
            impl_->lastFullSize = wsize;
            impl_->minimized    = true;
        }
        ImGui::PopID();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
    }

    // ─── Sidebar ───
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
        tabBtn("JS", 1);
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

    // ─── 5-zone resize edges + corner ───
    if (!impl_->fullscreen) {
        ImVec2 wPos  = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        const float edge = 48.0f;

        // Top edge → drag
        ImGui::SetCursorScreenPos(ImVec2(wPos.x + edge, wPos.y + 2));
        ImGui::InvisibleButton("##ztop", ImVec2(wSize.x - 2*edge, edge),
            ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive()) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(wPos.x + d.x, wPos.y + d.y));
        }

        // Bottom edge → resize height
        ImGui::SetCursorScreenPos(ImVec2(wPos.x + edge,
                                          wPos.y + wSize.y - edge));
        ImGui::InvisibleButton("##zbot", ImVec2(wSize.x - 2*edge, edge),
            ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive()) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            float nh = wSize.y + d.y;
            if (nh < kMinH) nh = kMinH;
            if (nh > io.DisplaySize.y) nh = io.DisplaySize.y;
            ImGui::SetWindowSize(ImVec2(wSize.x, nh));
        }

        // Left edge → resize width
        ImGui::SetCursorScreenPos(ImVec2(wPos.x, wPos.y + edge));
        ImGui::InvisibleButton("##zleft", ImVec2(edge, wSize.y - 2*edge),
            ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive()) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            float nw = wSize.x - d.x;
            if (nw < kMinW) nw = kMinW;
            if (nw > io.DisplaySize.x) nw = io.DisplaySize.x;
            ImGui::SetWindowSize(ImVec2(nw, wSize.y));
        }

        // Right edge → resize width
        ImGui::SetCursorScreenPos(ImVec2(wPos.x + wSize.x - edge,
                                          wPos.y + edge));
        ImGui::InvisibleButton("##zright", ImVec2(edge, wSize.y - 2*edge),
            ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive()) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            float nw = wSize.x + d.x;
            if (nw < kMinW) nw = kMinW;
            if (nw > io.DisplaySize.x) nw = io.DisplaySize.x;
            ImGui::SetWindowSize(ImVec2(nw, wSize.y));
        }

        // Corner (bottom-right) → resize both
        ImGui::SetCursorScreenPos(
            ImVec2(wPos.x + wSize.x - 100, wPos.y + wSize.y - 100));
        ImGui::InvisibleButton("##zcorner", ImVec2(100, 100),
            ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive()) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            float nw = wSize.x + d.x;
            float nh = wSize.y + d.y;
            if (nw < kMinW) nw = kMinW;
            if (nh < kMinH) nh = kMinH;
            if (nw > io.DisplaySize.x) nw = io.DisplaySize.x;
            if (nh > io.DisplaySize.y) nh = io.DisplaySize.y;
            ImGui::SetWindowSize(ImVec2(nw, nh));
        }
    }

    // ─── Unified drag from any empty spot ───
    if (!impl_->fullscreen) {
        bool hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows
            | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && hovered
            && !ImGui::IsAnyItemActive()) {
            impl_->windowDragging  = true;
            impl_->dragStartMouse  = ImGui::GetIO().MousePos;
            impl_->dragStartWindow = ImGui::GetWindowPos();
        }

        if (impl_->windowDragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImVec2 mp = ImGui::GetIO().MousePos;
                ImVec2 d(mp.x - impl_->dragStartMouse.x,
                         mp.y - impl_->dragStartMouse.y);
                ImGui::SetWindowPos(ImVec2(impl_->dragStartWindow.x + d.x,
                                           impl_->dragStartWindow.y + d.y));
            } else {
                impl_->windowDragging = false;
            }
        }
    }

    ImGui::End();
}

void MainWindow::drawMinimized() {
    ImGuiIO& io = ImGui::GetIO();
    const float size = 72.0f;

    if (impl_->minimizedPos.x + size > io.DisplaySize.x)
        impl_->minimizedPos.x = io.DisplaySize.x - size - 8;
    if (impl_->minimizedPos.y + size > io.DisplaySize.y)
        impl_->minimizedPos.y = io.DisplaySize.y - size - 8;
    if (impl_->minimizedPos.x < 0) impl_->minimizedPos.x = 0;
    if (impl_->minimizedPos.y < 0) impl_->minimizedPos.y = 0;

    ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(impl_->minimizedPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size, size), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, size * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ImVec4(0.02f, 0.02f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border,
        ImVec4(0.85f, 0.65f, 0.00f, 1.0f));

    ImGui::Begin("##YAMGG_Minimized", nullptr, f);

    ImVec2 curPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##yg_icon", ImVec2(size, size));
    bool active = ImGui::IsItemActive();

    ImVec2 center(curPos.x + size * 0.5f, curPos.y + size * 0.5f);
    ImVec2 textSz = ImGui::CalcTextSize("YG");
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(center.x - textSz.x * 0.5f, center.y - textSz.y * 0.5f),
        IM_COL32(255, 215, 0, 255), "YG");

    if (active) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        impl_->minimizedPos.x += d.x;
        impl_->minimizedPos.y += d.y;

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (std::fabs(d.x) < 3.0f && std::fabs(d.y) < 3.0f) {
                impl_->minimized = false;
                if (impl_->lastFullSize.x > 100.0f) {
                    ImGui::SetNextWindowPos(impl_->lastFullPos,
                        ImGuiCond_Appearing);
                    ImGui::SetNextWindowSize(impl_->lastFullSize,
                        ImGuiCond_Appearing);
                }
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
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
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::Text("YAM-GG | scripts: %d | running: %d",
                    JSConsole::instance().scriptCount(),
                    JSConsole::instance().runningCount());
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void MainWindow::drawMenuBar() {
    // disabled per user request
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
