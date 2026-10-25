#include "toast_queue.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#include "toast_rule.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Toast {
    std::string text;
    toast_queue::Style style;
    Clock::time_point start;
    float seconds;
};

std::mutex g_mutex;
std::vector<Toast> g_toasts;

float secondsSince(Clock::time_point from, Clock::time_point now) { return std::chrono::duration<float>(now - from).count(); }

}  // namespace

namespace toast_queue {

void push(const char* text, float seconds, Style style) {
    std::lock_guard lock(g_mutex);
    g_toasts.push_back({text, style, Clock::now(), seconds});
}

std::vector<Visible> visible() {
    const auto now = Clock::now();
    std::lock_guard lock(g_mutex);
    std::erase_if(g_toasts, [now](const Toast& toast) { return secondsSince(toast.start, now) >= toast.seconds; });
    std::vector<Visible> out;
    const size_t first = g_toasts.size() > kMaxVisible ? g_toasts.size() - kMaxVisible : 0;
    for (size_t i = first; i < g_toasts.size(); ++i) {
        const Toast& toast = g_toasts[i];
        out.push_back({toast.text, toast.style, toast_rule::alpha(secondsSince(toast.start, now), toast.seconds)});
    }
    return out;
}

}  // namespace toast_queue
