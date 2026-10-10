#ifndef YAMGG_UI_TABS_JSCONSOLE_H
#define YAMGG_UI_TABS_JSCONSOLE_H

#include <string>
#include <vector>
#include <mutex>

namespace yamgg {

class JSConsole {
public:
    static JSConsole& instance();
    void draw();
    void evaluate(const std::string& code);
    void loadScriptFromFile(const std::string& path);
    void unloadAll();
    int  scriptCount() const;
    int  runningCount() const;
    void pushOutput(const std::string& line);

private:
    JSConsole();
    ~JSConsole();
    JSConsole(const JSConsole&) = delete;
    JSConsole& operator=(const JSConsole&) = delete;

    struct ScriptEntry {
        std::string name;
        std::string path;
        std::string code;
        bool running{false};
        bool selected{false};
        int  id{0};
    };

    void drawLogTab();
    void drawConsoleTab();
    void loadSelected();
    void unloadSelected();
    void forgetScripts();
    void clearOutput();
    void registerEvents();

    std::vector<ScriptEntry> scripts_;
    std::vector<std::string> output_;
    mutable std::mutex mu_;
    int nextScriptId_{1};
    int activeSubTab_{0};
    bool eventsRegistered_{false};
    char codeBuffer_[65536]{0};
};

#define YAMGG_JSCONSOLE yamgg::JSConsole::instance()
} // namespace yamgg

#endif
