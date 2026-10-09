#ifndef YAMGG_UI_TABS_FILEBROWSER_H
#define YAMGG_UI_TABS_FILEBROWSER_H

#include <string>
#include <vector>
#include <functional>

namespace yamgg {

class FileBrowser {
public:
    static FileBrowser& instance();

    void open(const std::string& initial = "/storage/emulated/0/");
    void close();
    bool isOpen() const { return open_; }

    void draw();

    void setOnSelect(std::function<void(const std::string&)> cb) { onSelect_ = std::move(cb); }

private:
    FileBrowser() = default;
    ~FileBrowser() = default;
    FileBrowser(const FileBrowser&) = delete;
    FileBrowser& operator=(const FileBrowser&) = delete;

    struct Entry {
        std::string name;
        std::string fullPath;
        bool isDir;
        long long size;
    };

    void refresh();
    void navigateTo(const std::string& path);
    void goUp();

    bool open_{false};
    std::string currentPath_;
    std::vector<Entry> entries_;
    std::function<void(const std::string&)> onSelect_;
    char filter_[128]{0};
    bool showHidden_{false};
    std::string error_;
};

#define YAMGG_FILEBROWSER yamgg::FileBrowser::instance()

} // namespace yamgg

#endif
