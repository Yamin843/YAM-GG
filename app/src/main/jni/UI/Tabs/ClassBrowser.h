#ifndef YAMGG_UI_TABS_CLASSBROWSER_H
#define YAMGG_UI_TABS_CLASSBROWSER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace yamgg {

class ClassBrowser {
public:
    static ClassBrowser& instance();

    void draw();

private:
    ClassBrowser();
    ~ClassBrowser();
    ClassBrowser(const ClassBrowser&) = delete;
    ClassBrowser& operator=(const ClassBrowser&) = delete;

    struct ParamInfo {
        std::string typeName;
        std::string jniType;
    };
    struct MethodInfo {
        std::string name;
        std::string ret;
        std::vector<ParamInfo> args;
    };

    void triggerLoadClasses();
    void triggerLoadMethods(const std::string& cls);
    void triggerTrace(const std::string& cls, const MethodInfo& m);
    void triggerCall(const std::string& cls, const MethodInfo& m);
    void registerEvents();
    void sendJS(const std::string& js);

    void drawTopBar();
    void drawClassesTree();
    void drawMethodsList();
    void drawMethodDetail();

    mutable std::mutex mu_;
    std::vector<std::string> classes_;
    std::unordered_map<std::string, std::vector<MethodInfo>> methodsCache_;
    std::string filter_;
    std::string selectedClass_;
    int selectedMethodIdx_{-1};
    bool loading_{false};
    bool eventsRegistered_{false};
    char filterBuf_[256]{0};
};

#define YAMGG_CLASSBROWSER yamgg::ClassBrowser::instance()

} // namespace yamgg

#endif
