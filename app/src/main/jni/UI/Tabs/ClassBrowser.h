#ifndef YAMGG_UI_TABS_CLASSBROWSER_H
#define YAMGG_UI_TABS_CLASSBROWSER_H

#include <string>
#include <vector>
#include <mutex>

namespace yamgg {

class ClassBrowser {
public:
    static ClassBrowser& instance();

    void draw();

    // Called when a class_probe reply arrives (future)
    void onClassesLoaded(const std::string& json);
    void onClassProbe(const std::string& className, const std::string& json);

private:
    ClassBrowser();
    ~ClassBrowser();
    ClassBrowser(const ClassBrowser&) = delete;
    ClassBrowser& operator=(const ClassBrowser&) = delete;

    struct ClassEntry { std::string name; };
    struct MethodEntry { std::string name; std::string sig; std::string ret; };

    void triggerLoad();
    void drawClassList();
    void drawMethodList();
    void drawParameterPanel();

    std::mutex mu_;
    std::vector<ClassEntry> classes_;
    std::vector<MethodEntry> methods_;
    std::string filter_;
    std::string selectedClass_;
    std::string selectedMethod_;
    bool loading_{false};
};

#define YAMGG_CLASSBROWSER yamgg::ClassBrowser::instance()

} // namespace yamgg

#endif
