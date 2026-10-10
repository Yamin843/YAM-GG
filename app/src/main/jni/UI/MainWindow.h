#ifndef YAMGG_UI_MAINWINDOW_H
#define YAMGG_UI_MAINWINDOW_H

#include <string>

namespace yamgg {

class MainWindow {
public:
    static MainWindow& instance();

    void draw();
    void show() { visible_ = true; }
    void hide() { visible_ = false; }
    void toggle() { visible_ = !visible_; }
    bool visible() const { return visible_; }

    void openFileBrowser();
    void closeFileBrowser();
    bool fileBrowserOpen() const;

    void notify(const std::string& msg, float duration = 3.0f);

    // Rect tracked during draw(). JNI calls these from any thread; the
    // implementation guards with a mutex in the .cpp.
    bool hitTest(float x, float y) const;
    bool getRect(float& x, float& y, float& w, float& h) const;
    void invalidateRect();

private:
    MainWindow();
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void drawMainWindow();
    void drawMinimized();
    void drawStatusBar();

    bool visible_{true};
    bool collapsed_{false};

    struct Impl;
    Impl* impl_;
};

#define YAMGG_MAINWINDOW yamgg::MainWindow::instance()

} // namespace yamgg

#endif
