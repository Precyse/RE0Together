#include "toast_queue.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace {

using Clock = std::chrono::steady_clock;

struct Toast {
    std::string text;
    Clock::time_point expiry;
};

std::mutex g_mutex;
std::vector<Toast> g_toasts;

}  // namespace

namespace toast_queue {

void push(const char* text, float seconds) {
    const auto expiry = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds));
    std::lock_guard lock(g_mutex);
    g_toasts.push_back({text, expiry});
}

std::vector<Visible> visible() {
    const auto now = Clock::now();
    std::lock_guard lock(g_mutex);
    std::erase_if(g_toasts, [now](const Toast& toast) { return toast.expiry <= now; });
    std::vector<Visible> out;
    const size_t first = g_toasts.size() > kMaxVisible ? g_toasts.size() - kMaxVisible : 0;
    for (size_t i = first; i < g_toasts.size(); ++i) {
        const float remaining = std::chrono::duration<float>(g_toasts[i].expiry - now).count();
        out.push_back({g_toasts[i].text, std::min(1.0f, remaining / kFadeSeconds)});
    }
    return out;
}

}  // namespace toast_queue
