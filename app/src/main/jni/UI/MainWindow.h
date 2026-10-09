
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

    // Returns true if (x, y) falls inside our current visible UI.
    // Used by ModView to decide whether to claim a touch gesture
    // (blocks the underlying app) or pass it through.
    bool hitTest(float x, float y) const;

private:
    MainWindow();
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void drawMainWindow();
    void drawMenuBar();
    void drawStatusBar();
    void drawMinimized();

    bool visible_{true};
    bool firstDraw_{true};
    bool collapsed_{false};

    struct Impl;
    Impl* impl_;
};

#define YAMGG_MAINWINDOW yamgg::MainWindow::instance()

} // namespace yamgg

#endif
