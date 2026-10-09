#ifndef YAMGG_UI_TABS_CLASSBROWSER_H
#define YAMGG_UI_TABS_CLASSBROWSER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace yamgg {

class ClassBrowser {
public:
    static ClassBrowser& instance();
    void draw();

private:
    ClassBrowser();
    ~ClassBrowser();

    struct KeyValue {
        char k[128];
        char v[256];
    };

    struct ParamInfo {
        std::string typeName;   // fully qualified
        std::string kind;       // int/long/float/double/boolean/char/string/map/array/enum/object
        std::vector<std::string> enumValues;
        // user input state:
        char scalar[512];       // for scalars/string/json textarea
        bool booleanVal{false};
        std::vector<KeyValue> kvs;   // for maps
    };

    struct MethodInfo {
        std::string name;
        std::string ret;
        std::string retKind;
        bool isStatic{true};
        bool tracing{false};
        std::vector<ParamInfo> args;
    };

    struct InstanceEntry {
        unsigned long long handle{0};
        std::string className;
        std::vector<std::pair<std::string, std::string>> fields;  // name->display
    };

    void registerEvents();
    void sendJS(const std::string& js);
    void triggerLoadClasses();
    void triggerLoadMethods(const std::string& cls);

    void traceOn(const std::string& cls, MethodInfo& m);
    void traceOff(const std::string& cls, MethodInfo& m);
    void callMethod(const std::string& cls, MethodInfo& m);
    void findInstances(const std::string& cls);

    void drawTopBar();
    void drawTree();
    void drawPackageGroup(const std::string& pkg, std::vector<std::string>& members);
    void drawClassNode(const std::string& cls);
    void drawMethodNode(const std::string& cls, MethodInfo& m);
    void drawParamWidget(const std::string& cls, MethodInfo& m, ParamInfo& p, int idx);
    void drawInstancePicker(const std::string& cls, MethodInfo& m);

    std::string buildArgsJSON(const MethodInfo& m);

    mutable std::mutex mu_;
    std::vector<std::string> classes_;
    std::map<std::string, std::vector<MethodInfo>> methods_;
    std::map<std::string, std::vector<InstanceEntry>> instances_;  // cls -> instances
    std::string filter_;
    std::string selectedClass_;
    int selectedInstance_{0};
    bool loading_{false};
    bool eventsRegistered_{false};
    char filterBuf_[256]{0};
};

#define YAMGG_CLASSBROWSER yamgg::ClassBrowser::instance()
} // namespace yamgg

#endif
