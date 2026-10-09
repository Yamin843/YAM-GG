#ifndef YAMGG_UI_THEME_H
#define YAMGG_UI_THEME_H

namespace yamgg {

class Theme {
public:
    static void apply();
    static void applyBlackGold();
    static void applyCompact();
    static void applyRounded();

    static void setFontSize(float px);
    static float fontSize();

private:
    Theme() = delete;
};

} // namespace yamgg

#endif
