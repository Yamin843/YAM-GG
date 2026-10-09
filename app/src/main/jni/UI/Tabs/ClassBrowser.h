#ifndef YAMGG_UI_TABS_CLASSBROWSER_H
#define YAMGG_UI_TABS_CLASSBROWSER_H

#include <string>
#include "../../all_libs/YAM-G/wrapper/include/yam.hpp"
#include <vector>
#include <map>
#include <mutex>
#include <set>

namespace yamgg {

class ClassBrowser {
public:
    static ClassBrowser& instance();
    void draw();

private:
    ClassBrowser();
    ~ClassBrowser();

    struct KeyValue { char k[128]; char v[256]; };
    struct ParamInfo {
        std::string typeName;
        std::string kind;
        std::vector<std::string> enumValues;
        char scalar[512];
        bool booleanVal{false};
        std::vector<KeyValue> kvs;
    };
    struct MethodInfo {
        std::string name, ret, retKind;
        bool isStatic{true};
        bool tracing{false};
        std::vector<ParamInfo> args;
    };
    struct FieldInfo {
        std::string name, type, value;
    };
    struct InstanceEntry {
        unsigned long long handle{0};
        std::string className;
        std::vector<std::pair<std::string, std::string>> fields;
    };

    void registerEvents();
    void sendJS(const std::string& js);
    void triggerLoadClasses();
    void triggerLoadMethods(const std::string& cls);
    void triggerTrace(const std::string& cls, MethodInfo& m);
    void triggerCall(const std::string& cls, MethodInfo& m);
    void findInstances(const std::string& cls);
    std::string buildArgsJSON(const MethodInfo& m);

    void drawSearchBar();
    void drawTree();
    void drawPackageGroup(const std::string& pkg, std::vector<std::string>& members);
    void drawClassNode(const std::string& cls);
    void drawMethodNode(const std::string& cls, MethodInfo& m);
    void drawParamWidget(const std::string& cls, MethodInfo& m, ParamInfo& p, int idx);
    void drawInstancePicker(const std::string& cls, MethodInfo& m);

    mutable std::mutex mu_;
    std::vector<std::string> classes_;
    std::map<std::string, std::vector<MethodInfo>> methods_;

    // مفاتيح = "className::methodName" — تبقى حتى بعد reload
    std::set<std::string> tracedMethods_;
    std::map<std::string, std::vector<FieldInfo>> fields_;
    std::map<std::string, std::vector<InstanceEntry>> instances_;
    std::string filter_;
    std::string selectedClass_;
    int selectedInstance_{0};
    bool loading_{false};
    bool eventsRegistered_{false};

    // Advanced filter checkboxes
    bool searchClass_{true};
    bool searchMethod_{false};
    bool searchField_{false};
    char searchBuf_[256]{0};
};

#define YAMGG_CLASSBROWSER yamgg::ClassBrowser::instance()
} // namespace yamgg
#endif
