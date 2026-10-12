#include "Theme.h"

#include "imgui.h"
#include "../../Includes/Roboto-Regular.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {
inline ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// Palette — warm gold on layered near-black. Not pure black, not neon.
static const ImVec4 kBgDeep      = rgba(0x08, 0x08, 0x0A);
static const ImVec4 kBgWindow    = rgba(0x0E, 0x0E, 0x11);
static const ImVec4 kBgChild     = rgba(0x0B, 0x0B, 0x0E);
static const ImVec4 kBgCard      = rgba(0x1A, 0x1A, 0x1F);
static const ImVec4 kBgCardHover = rgba(0x24, 0x24, 0x2A);
static const ImVec4 kBgCardAct   = rgba(0x2E, 0x2E, 0x36);

static const ImVec4 kGoldBase    = rgba(0xD4, 0xA0, 0x17);
static const ImVec4 kGoldBright  = rgba(0xF0, 0xBE, 0x3C);
static const ImVec4 kGoldMuted   = rgba(0xA8, 0x80, 0x1A);
static const ImVec4 kGoldSoft    = rgba(0xD4, 0xA0, 0x17, 0.35f);

static const ImVec4 kText        = rgba(0xF2, 0xF2, 0xF4);
static const ImVec4 kTextDim     = rgba(0xA8, 0xA8, 0xAE);
static const ImVec4 kTextFaint   = rgba(0x62, 0x62, 0x68);
} // namespace

float g_fontSize = 26.0f;

void Theme::apply() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Rounding — moderate, modern
    style.WindowRounding     = 14.0f;
    style.ChildRounding      = 10.0f;
    style.FrameRounding      = 10.0f;
    style.PopupRounding      = 12.0f;
    style.TabRounding        = 10.0f;
    style.GrabRounding       = 10.0f;
    style.ScrollbarRounding  = 10.0f;

    // Borders — subtle but visible
    style.WindowBorderSize   = 2.0f;
    style.ChildBorderSize    = 0.0f;
    style.FrameBorderSize    = 0.5f;
    style.PopupBorderSize    = 2.0f;
    style.TabBorderSize      = 0.0f;

    // Spacing
    style.WindowPadding      = ImVec2(16.0f, 14.0f);
    style.FramePadding       = ImVec2(14.0f, 10.0f);
    style.CellPadding        = ImVec2(10.0f, 8.0f);
    style.ItemSpacing        = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing   = ImVec2(8.0f, 6.0f);
    style.IndentSpacing      = 20.0f;
    style.ScrollbarSize      = 16.0f;
    style.GrabMinSize        = 24.0f;

    // Alignment
    style.WindowTitleAlign        = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign         = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign     = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ColorButtonPosition     = ImGuiDir_Right;

    // Anti-aliasing
    style.AntiAliasedLines        = true;
    style.AntiAliasedLinesUseTex  = true;
    style.AntiAliasedFill         = true;
    style.CircleTessellationMaxError = 0.15f;

    // Colors — text
    colors[ImGuiCol_Text]                 = kText;
    colors[ImGuiCol_TextDisabled]         = kTextFaint;

    // Colors — surfaces
    colors[ImGuiCol_WindowBg]             = kBgWindow;
    colors[ImGuiCol_ChildBg]              = kBgChild;
    colors[ImGuiCol_PopupBg]              = rgba(0x12, 0x12, 0x16, 0.98f);
    colors[ImGuiCol_MenuBarBg]            = kBgWindow;

    // Borders
    colors[ImGuiCol_Border]               = kGoldMuted;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    // Frames
    colors[ImGuiCol_FrameBg]              = kBgCard;
    colors[ImGuiCol_FrameBgHovered]       = kBgCardHover;
    colors[ImGuiCol_FrameBgActive]        = kBgCardAct;

    // Title
    colors[ImGuiCol_TitleBg]              = kBgDeep;
    colors[ImGuiCol_TitleBgActive]        = rgba(0x14, 0x11, 0x08);
    colors[ImGuiCol_TitleBgCollapsed]     = kBgDeep;

    // Scrollbar — thin, semi-transparent
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab]        = rgba(0xFF, 0xFF, 0xFF, 0.20f);
    colors[ImGuiCol_ScrollbarGrabHovered] = kGoldSoft;
    colors[ImGuiCol_ScrollbarGrabActive]  = kGoldBase;

    // Interactive
    colors[ImGuiCol_CheckMark]            = kGoldBright;
    colors[ImGuiCol_SliderGrab]           = kGoldBase;
    colors[ImGuiCol_SliderGrabActive]     = kGoldBright;

    // Buttons
    colors[ImGuiCol_Button]               = kBgCard;
    colors[ImGuiCol_ButtonHovered]        = rgba(0x25, 0x1E, 0x0A);
    colors[ImGuiCol_ButtonActive]         = rgba(0x3A, 0x2C, 0x0E);

    // Headers (tree nodes, selectables)
    colors[ImGuiCol_Header]               = rgba(0x1A, 0x1A, 0x1E);
    colors[ImGuiCol_HeaderHovered]        = rgba(0x25, 0x20, 0x10);
    colors[ImGuiCol_HeaderActive]         = rgba(0x3A, 0x2E, 0x12);

    // Separators — thin gold
    colors[ImGuiCol_Separator]            = rgba(0x8A, 0x68, 0x10, 0.55f);
    colors[ImGuiCol_SeparatorHovered]     = kGoldBase;
    colors[ImGuiCol_SeparatorActive]      = kGoldBright;

    // Resize grips — hidden (we use custom 5-zone)
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0, 0, 0, 0);

    // Tabs
    colors[ImGuiCol_Tab]                  = rgba(0x12, 0x12, 0x14);
    colors[ImGuiCol_TabHovered]           = rgba(0x1E, 0x19, 0x0A);
    colors[ImGuiCol_TabSelected]          = rgba(0x2A, 0x22, 0x0E);
    colors[ImGuiCol_TabDimmed]            = rgba(0x0E, 0x0E, 0x10);
    colors[ImGuiCol_TabDimmedSelected]    = rgba(0x1A, 0x14, 0x08);
    colors[ImGuiCol_TabSelectedOverline]  = kGoldBright;
    colors[ImGuiCol_TabDimmedSelectedOverline] = kGoldMuted;

    // Plot
    colors[ImGuiCol_PlotLines]            = kGoldBase;
    colors[ImGuiCol_PlotLinesHovered]     = kGoldBright;
    colors[ImGuiCol_PlotHistogram]        = kGoldBase;
    colors[ImGuiCol_PlotHistogramHovered] = kGoldBright;

    // Tables
    colors[ImGuiCol_TableHeaderBg]        = rgba(0x14, 0x12, 0x0A);
    colors[ImGuiCol_TableBorderStrong]    = rgba(0x8A, 0x68, 0x10, 0.45f);
    colors[ImGuiCol_TableBorderLight]     = rgba(0x2A, 0x2A, 0x2E);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]        = rgba(0xFF, 0xFF, 0xFF, 0.02f);

    // Selection / nav
    colors[ImGuiCol_TextSelectedBg]       = kGoldSoft;
    colors[ImGuiCol_DragDropTarget]       = kGoldBright;
    colors[ImGuiCol_NavHighlight]         = kGoldBright;
    colors[ImGuiCol_NavWindowingHighlight]= rgba(0xFF, 0xFF, 0xFF, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg]    = rgba(0, 0, 0, 0.6f);
    colors[ImGuiCol_ModalWindowDimBg]     = rgba(0, 0, 0, 0.7f);

    // Font
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH        = 2;
    cfg.OversampleV        = 1;
    cfg.PixelSnapH         = false;
    cfg.RasterizerMultiply = 1.10f;

    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        (void*)Roboto_Regular, (int)sizeof(Roboto_Regular),
        g_fontSize, &cfg);
    if (!font) {
        LOGI("Theme: Roboto failed, using default");
        io.Fonts->AddFontDefault();
    }
    LOGI("Theme applied (font=%p size=%.1f)", (void*)font, g_fontSize);
}

void Theme::applyBlackGold() { apply(); }

void Theme::applyCompact() {
    apply();
    ImGuiStyle& s = ImGui::GetStyle();
    s.FramePadding = ImVec2(10.0f, 7.0f);
    s.ItemSpacing = ImVec2(7.0f, 6.0f);
}

void Theme::applyRounded() {
    apply();
    ImGuiStyle& s = ImGui::GetStyle();
    s.FrameRounding  = 22.0f;
    s.WindowRounding = 26.0f;
    s.PopupRounding  = 22.0f;
}

void Theme::setFontSize(float px) { g_fontSize = px; }
float Theme::fontSize() { return g_fontSize; }

} // namespace yamgg
