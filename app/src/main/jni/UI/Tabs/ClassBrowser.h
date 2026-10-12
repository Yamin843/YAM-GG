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

    struct KVPair { char key[256]{0}; char value[256]{0}; int valueType{0}; };
    struct ParamInfo {
        std::string typeName;
        std::string kind;        // int/string/list/map/date/class/atomic/optional/...
        std::string generic;
        std::vector<std::string> enumValues;
        char scalar[512]{0};
        char dateStr[32]{0};
        char nestedClass[256]{0};
        bool booleanVal{false};
        std::vector<KVPair> kvPairs;
        std::vector<std::string> listItems;
        int  listItemType{0};
        char jsonSpec[4096]{0};
    };
    struct MethodInfo {
        std::string name, ret, retKind;
        bool isStatic{true};
        bool tracing{false};
        int  argTraceIdx{-1};
        std::vector<ParamInfo> args;
    };
    struct FieldInfo {
        std::string name, type, typeKind, value;
        bool isStatic{false};
        bool watching{false};
    };
    struct CtorInfo { std::vector<ParamInfo> args; };
    struct InstanceEntry {
        unsigned long long handle{0};
        std::string className;
        std::vector<std::pair<std::string, std::string>> fields;
    };
    struct MemberMatch {
        std::string className, name, kind, ret;
        int  argCount{0};
        bool isStatic{false};
    };
    struct CallState {
        bool active{false};
        bool isCtor{false};
        std::string className, methodName, returnType;
        bool isStatic{true};
        std::vector<ParamInfo> args;
        std::string lastResult;
        bool lastOk{false};
        bool haveResult{false};
    };

    void registerEvents();
    void sendCommand(const std::string& json);

    void triggerLoadClasses();
    void triggerLoadMethods(const std::string& cls);
    void triggerTrace(const std::string& cls, MethodInfo& m);
    void triggerTraceArg(const std::string& cls, MethodInfo& m, int idx);
    void triggerUntrace(const std::string& cls, MethodInfo& m);
    void triggerFieldRead(const std::string& cls, FieldInfo& f);
    void triggerFieldWatch(const std::string& cls, FieldInfo& f);
    void triggerFieldUnwatch(const std::string& cls, FieldInfo& f);
    void findInstances(const std::string& cls);
    void triggerMemberSearch();

    void pushTraceLog(const std::string& line);
    void activateCall(const std::string& cls, MethodInfo& m);
    void activateCallCtor(const std::string& cls, CtorInfo& c);
    void fireCall();

    std::string argsSpecFor(const std::vector<ParamInfo>& args);

    void drawClassesTab();
    void drawTraceLogTab();
    void drawCallMethodTab();

    void drawSearchBar();
    void drawMemberMatches();
    void drawTree();
    void drawPackageGroup(const std::string& pkg, std::vector<std::string>& members);
    void drawClassNode(const std::string& cls);
    void drawMethodNode(const std::string& cls, MethodInfo& m);
    void drawCtorNode(const std::string& cls, CtorInfo& c);
    void drawFieldNode(const std::string& cls, FieldInfo& f);
    void drawParamWidget(ParamInfo& p, int idx);
    void drawInstancePicker(const std::string& cls);

    mutable std::mutex mu_;
    std::vector<std::string> classes_;
    std::vector<std::string> filtered_;
    std::string lastQuery_;
    std::map<std::string, std::vector<MethodInfo>> methods_;
    std::map<std::string, std::vector<FieldInfo>> fields_;
    std::map<std::string, std::vector<CtorInfo>> ctors_;
    std::map<std::string, std::vector<InstanceEntry>> instances_;
    std::set<std::string> tracedMethods_;
    std::set<std::string> pendingClasses_;
    std::vector<MemberMatch> memberMatches_;

    int  activeSubTab_{0};
    int  pendingSubTab_{-1};
    bool pendingSwitchToCall_{false};
    CallState callState_;
    std::vector<std::string> traceLog_;
    bool traceLogAutoScroll_{true};
    bool traceLogPaused_{false};

    std::string filter_;
    std::string selectedClass_;
    int  selectedInstance_{0};
    volatile bool loading_{false};
    bool eventsRegistered_{false};
    bool chunksRegistered_{false};
    bool autoLoadTried_{false};

    bool searchClass_{true};
    bool searchMethod_{false};
    bool searchField_{false};
    char searchBuf_[256]{0};
    float autoSearchAt_{0.0f};
    std::string autoSearchLast_;
    char memberSearch_[128]{0};
};

#define YAMGG_CLASSBROWSER yamgg::ClassBrowser::instance()
} // namespace yamgg

#endif
