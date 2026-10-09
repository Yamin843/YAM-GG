#ifndef YAMGG_UI_RENDERER_H
#define YAMGG_UI_RENDERER_H

#include <mutex>
#include <atomic>

namespace yamgg {

class Renderer {
public:
    static Renderer& instance();

    void onSurfaceCreated();
    void onSurfaceChanged(int width, int height);
    void onDrawFrame(int width, int height);
    void onTouch(int action, float x, float y, int pointerId);

    bool isReady() const { return ready_.load(); }
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
};

#define YAMGG_RENDERER yamgg::Renderer::instance()

} // namespace yamgg

#endif
