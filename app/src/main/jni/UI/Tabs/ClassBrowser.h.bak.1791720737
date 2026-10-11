#ifndef YAMGG_UI_TABS_CLASSBROWSER_H
#define YAMGG_UI_TABS_CLASSBROWSER_H

#include <string>
#include <vector>
#include <map>
#include <set>
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
        std::string kind;              // int/string/enum/list/map/... 
        std::string generic;           // e.g. Map<String,Foo>
        std::vector<std::string> enumValues;
        // Simple editors
        char scalar[512]{0};
        bool booleanVal{false};
        // Complex editor (JSON text, prefilled with preset)
        char jsonSpec[4096]{0};
    };
    struct MethodInfo {
        std::string name, ret, retKind;
        bool isStatic{true};
        bool tracing{false};
        std::vector<ParamInfo> args;
    };
    struct FieldInfo {
        std::string name, type, typeKind, value;
        bool isStatic{false};
    };
    struct CtorInfo { std::vector<ParamInfo> args; };
    struct InstanceEntry {
        unsigned long long handle{0};
        std::string className;
        std::vector<std::pair<std::string, std::string>> fields;
    };

    void registerEvents();
    void sendCommand(const std::string& json);
    void triggerLoadClasses();
    void triggerLoadMethods(const std::string& cls);
    void triggerTrace(const std::string& cls, MethodInfo& m);
    void triggerUntrace(const std::string& cls, MethodInfo& m);
    void triggerCall(const std::string& cls, MethodInfo& m);
    void triggerNewInstance(const std::string& cls, CtorInfo& c);
    void findInstances(const std::string& cls);

    void drawSearchBar();
    void drawTree();
    void drawPackageGroup(const std::string& pkg,
                          std::vector<std::string>& members);
    void drawClassNode(const std::string& cls);
    void drawMethodNode(const std::string& cls, MethodInfo& m);
    void drawCtorNode(const std::string& cls, CtorInfo& c);
    void drawParamWidget(ParamInfo& p, int idx);
    void drawInstancePicker(const std::string& cls);

    std::string argsSpecFor(const std::vector<ParamInfo>& args);

    mutable std::mutex mu_;
    std::vector<std::string> classes_;
    std::map<std::string, std::vector<MethodInfo>> methods_;
    std::map<std::string, std::vector<FieldInfo>> fields_;
    std::map<std::string, std::vector<CtorInfo>> ctors_;
    std::map<std::string, std::vector<InstanceEntry>> instances_;
    std::set<std::string> tracedMethods_;

    std::string filter_;
    std::string selectedClass_;
    int  selectedInstance_{0};
    volatile bool loading_{false};
    bool eventsRegistered_{false};
    bool autoLoadTried_{false};

    bool searchClass_{true};
    bool searchMethod_{false};
    bool searchField_{false};
    char searchBuf_[256]{0};
};

#define YAMGG_CLASSBROWSER yamgg::ClassBrowser::instance()
} // namespace yamgg

#endif
