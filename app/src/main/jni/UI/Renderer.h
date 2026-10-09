#ifndef YAMGG_UI_RENDERER_H
#define YAMGG_UI_RENDERER_H

#include <mutex>
#include <vector>
#include <atomic>

namespace yamgg {

class Renderer {
public:
    static Renderer& instance();

    void onSurfaceCreated();
    void onSurfaceChanged(int width, int height);
    void onDrawFrame(int width, int height);
    void onTouch(int action, float x, float y, int pointerId);
    void onChar(unsigned int codepoint);
    void onScroll(float dx, float dy);
    void onKey(int keyCode, int action);   // action: 0=down, 1=up
    bool wantTextInput() const { return wantTextInput_.load(); }

    bool isReady() const { return ready_.load(); }
    bool wantCaptureMouse() const { return wantCaptureMouse_.load(); }
    int width() const { return width_; }
    int height() const { return height_; }

    void shutdown();

private:
    Renderer() = default;
    ~Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    std::mutex mu_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> initialized_{false};
    int width_{0};
    int height_{0};
    double lastFrameTime_{0.0};
    bool backendInit_{false};
    std::atomic<bool> wantCaptureMouse_{false};
    std::atomic<bool> wantTextInput_{false};
    std::mutex charMu_;
    std::vector<unsigned int> charQueue_;
};

#define YAMGG_RENDERER yamgg::Renderer::instance()

} // namespace yamgg

#endif
