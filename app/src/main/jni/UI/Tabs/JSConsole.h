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
    void drawScriptsTab();

    void evaluate(const std::string& code);
    void loadScriptFromFile(const std::string& path);
    void reloadScripts();
    void unloadAll();
    void unloadScript(int index);

    int scriptCount() const;
    int runningCount() const;

    void pushOutput(const std::string& line);

private:
    JSConsole();
    ~JSConsole();
    JSConsole(const JSConsole&) = delete;
    JSConsole& operator=(const JSConsole&) = delete;

    struct Script {
        std::string name;
        std::string path;
        std::string code;
        bool running;
        bool isFile;
        int id;
    };

    void drawInput();
    void drawOutput();
    void addHistory(const std::string& cmd);

    std::string input_;
    std::vector<std::string> output_;
    std::vector<std::string> history_;
    int historyPos_;
    std::vector<Script> scripts_;
    std::mutex mu_;
    bool autoscroll_{true};
    bool wrapText_{true};
    int nextScriptId_{1};
    char loadPath_[512]{0};
};

#define YAMGG_JSCONSOLE yamgg::JSConsole::instance()

} // namespace yamgg

#endif
