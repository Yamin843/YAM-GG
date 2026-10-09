#include "Renderer.h"
#include "Theme.h"
#include "MainWindow.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"

#include <android/log.h>
#include <GLES3/gl3.h>
#include <chrono>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

Renderer& Renderer::instance() {
    static Renderer inst;
    return inst;
}

void Renderer::onSurfaceCreated() {
    std::lock_guard<std::mutex> lk(mu_);
    if (initialized_.load()) {
        LOGI("Renderer: already initialized");
        return;
    }

    LOGI("Renderer: creating ImGui context");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    Theme::apply();

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("Renderer: ImGui_ImplOpenGL3_Init failed");
        ImGui::DestroyContext();
        return;
    }
    backendInit_ = true;

    initialized_.store(true);
    ready_.store(true);
    LOGI("Renderer: initialized");
}

void Renderer::onSurfaceChanged(int width, int height) {
    std::lock_guard<std::mutex> lk(mu_);
    width_ = width;
    height_ = height;
    LOGI("Renderer: surface changed %dx%d", width, height);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
}

void Renderer::onDrawFrame(int width, int height) {
    // Re-entrancy guard: GLSurfaceView may call onDrawFrame while
    // onSurfaceChanged is still in flight on a different thread.
    static thread_local bool inDraw = false;
    if (inDraw) return;
    inDraw = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{inDraw};

    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;

    width_ = width;
    height_ = height;

    // Skip render when window is hidden
    if (!MainWindow::instance().visible()) {
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);

    auto now = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(now.time_since_epoch()).count();
    float dt = (lastFrameTime_ > 0.0) ? (float)(t - lastFrameTime_) : 1.0f / 60.0f;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    if (dt > 0.25f) dt = 0.25f;
    io.DeltaTime = dt;
    lastFrameTime_ = t;

    ImGui_ImplOpenGL3_NewFrame();

    // Drain char queue before NewFrame so ImGui sees them as input
    {
        std::lock_guard<std::mutex> lk(charMu_);
        for (auto cp : charQueue_) {
            io.AddInputCharacter(cp);
        }
        charQueue_.clear();
    }

    ImGui::NewFrame();

    MainWindow::instance().draw();

    // Capture whether ImGui is currently consuming mouse events
    // (cursor over a window). This drives ModView.dispatchTouchEvent.
    {
        ImGuiIO& io2 = ImGui::GetIO();
        bool capture = io2.WantCaptureMouse || io2.WantCaptureMouseUnlessPopupClose;
        wantCaptureMouse_.store(capture, std::memory_order_relaxed);
    }

    wantTextInput_.store(io.WantTextInput, std::memory_order_relaxed);
    ImGui::Render();

    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::onTouch(int action, float x, float y, int pointerId) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;

    ImGuiIO& io = ImGui::GetIO();
    (void)pointerId;

    // Android MotionEvent actions:
    // 0 = ACTION_DOWN
    // 1 = ACTION_UP
    // 2 = ACTION_MOVE
    // 3 = ACTION_CANCEL
    // 5 = ACTION_POINTER_DOWN
    // 6 = ACTION_POINTER_UP
    switch (action) {
        case 0:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;
        case 1:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            break;
        case 2:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            break;
        case 3:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            io.AddMouseButtonEvent(0, false);
            break;
        case 5:  // ACTION_POINTER_DOWN
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;
        case 6:  // ACTION_POINTER_UP
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            break;
        default:
            break;
    }
}


namespace {

// Android KeyEvent codes (android.view.KeyEvent constants).
enum {
    AK_BACK           = 4,
    AK_DPAD_UP        = 19,
    AK_DPAD_DOWN      = 20,
    AK_DPAD_LEFT      = 21,
    AK_DPAD_RIGHT     = 22,
    AK_DPAD_CENTER    = 23,
    AK_ALT_LEFT       = 57,
    AK_ALT_RIGHT      = 58,
    AK_SHIFT_LEFT     = 59,
    AK_SHIFT_RIGHT    = 60,
    AK_TAB            = 61,
    AK_ENTER          = 66,
    AK_DEL            = 67,   // backspace
    AK_ESCAPE         = 111,
    AK_FORWARD_DEL    = 112,
    AK_CTRL_LEFT      = 113,
    AK_CTRL_RIGHT     = 114,
    AK_MOVE_HOME      = 122,
    AK_MOVE_END       = 123,
    AK_PAGE_UP        = 92,
    AK_PAGE_DOWN      = 93,
    AK_INSERT         = 124,
};

ImGuiKey android_to_imgui_key(int code) {
    switch (code) {
    case AK_BACK:           return ImGuiKey_Escape;
    case AK_DPAD_UP:        return ImGuiKey_UpArrow;
    case AK_DPAD_DOWN:      return ImGuiKey_DownArrow;
    case AK_DPAD_LEFT:      return ImGuiKey_LeftArrow;
    case AK_DPAD_RIGHT:     return ImGuiKey_RightArrow;
    case AK_DPAD_CENTER:    return ImGuiKey_Enter;
    case AK_ALT_LEFT:       return ImGuiKey_LeftAlt;
    case AK_ALT_RIGHT:      return ImGuiKey_RightAlt;
    case AK_SHIFT_LEFT:     return ImGuiKey_LeftShift;
    case AK_SHIFT_RIGHT:    return ImGuiKey_RightShift;
    case AK_TAB:            return ImGuiKey_Tab;
    case AK_ENTER:          return ImGuiKey_Enter;
    case AK_DEL:            return ImGuiKey_Backspace;
    case AK_ESCAPE:         return ImGuiKey_Escape;
    case AK_FORWARD_DEL:    return ImGuiKey_Delete;
    case AK_CTRL_LEFT:      return ImGuiKey_LeftCtrl;
    case AK_CTRL_RIGHT:     return ImGuiKey_RightCtrl;
    case AK_MOVE_HOME:      return ImGuiKey_Home;
    case AK_MOVE_END:       return ImGuiKey_End;
    case AK_PAGE_UP:        return ImGuiKey_PageUp;
    case AK_PAGE_DOWN:      return ImGuiKey_PageDown;
    case AK_INSERT:         return ImGuiKey_Insert;
    default:                return ImGuiKey_None;
    }
}

} // namespace

void Renderer::onKey(int keyCode, int action) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;

    ImGuiIO& io = ImGui::GetIO();

    // Always mark the source as keyboard so text fields work.
    io.AddKeyEvent(ImGuiMod_None, false);   // reset implicit mods — we send explicit ones below

    ImGuiKey k = android_to_imgui_key(keyCode);
    if (k != ImGuiKey_None) {
        bool down = (action == 0);
        io.AddKeyEvent(k, down);
    } else {
        // fall back to a raw map so anything we missed still gets caught
        // (currently unused)
    }
}

void Renderer::onChar(unsigned int codepoint) {
    std::lock_guard<std::mutex> lk(charMu_);
    charQueue_.push_back(codepoint);
}

void Renderer::onScroll(float dx, float dy) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(dx, dy);
}

void Renderer::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;
    if (backendInit_) {
        ImGui_ImplOpenGL3_Shutdown();
        backendInit_ = false;
    }
    ImGui::DestroyContext();
    initialized_.store(false);
    ready_.store(false);
    LOGI("Renderer: shutdown");
}

} // namespace yamgg
