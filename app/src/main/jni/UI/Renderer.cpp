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
    std::lock_guard<std::mutex> lk(mu_);
    if (!initialized_.load()) return;

    width_ = width;
    height_ = height;

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
    ImGui::NewFrame();

    MainWindow::instance().draw();

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
