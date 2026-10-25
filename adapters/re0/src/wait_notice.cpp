#include "wait_notice.h"

#include <mutex>
#include <utility>

namespace {

std::mutex g_mutex;
std::optional<wait_notice::Shown> g_shown;  // guarded by g_mutex

}  // namespace

namespace wait_notice {

void show(wait_notice_rule::Notice notice, std::string text) {
    std::lock_guard lock(g_mutex);
    g_shown = Shown{notice, std::move(text)};
}

void clear() {
    std::lock_guard lock(g_mutex);
    g_shown.reset();
}

std::optional<Shown> current() {
    std::lock_guard lock(g_mutex);
    return g_shown;
}

}  // namespace wait_notice
