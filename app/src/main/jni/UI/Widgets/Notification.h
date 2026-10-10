#ifndef YAMGG_UI_WIDGETS_NOTIFICATION_H
#define YAMGG_UI_WIDGETS_NOTIFICATION_H

#include <string>
#include <deque>
#include <chrono>

namespace yamgg {

class Notification {
public:
    static Notification& instance();

    void push(const std::string& msg, float duration = 3.0f);
    void draw();
    void clear();

private:
    Notification() = default;
    ~Notification() = default;
    Notification(const Notification&) = delete;
    Notification& operator=(const Notification&) = delete;

    struct Item {
        int         id;
        std::string msg;
        std::chrono::steady_clock::time_point created;
        float       duration;
    };

    std::deque<Item> items_;
    int next_id_{1};
};

#define YAMGG_NOTIFY(msg) yamgg::Notification::instance().push(msg)

} // namespace yamgg

#endif
