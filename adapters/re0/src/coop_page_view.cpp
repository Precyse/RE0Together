#include "coop_page_view.h"

#include <chrono>
#include <mutex>

namespace {

using Clock = std::chrono::steady_clock;

std::mutex g_mutex;
std::vector<coop_page_rule::Row> g_rows;  // empty: the page is closed; guarded by g_mutex
Clock::time_point g_published;            // guarded by g_mutex

}  // namespace

namespace coop_page_view {

void publish(std::vector<coop_page_rule::Row> rows) {
    std::lock_guard lock(g_mutex);
    g_rows = std::move(rows);
    g_published = Clock::now();
}

void clear() {
    std::lock_guard lock(g_mutex);
    g_rows.clear();
}

std::vector<coop_page_rule::Row> current() {
    std::lock_guard lock(g_mutex);
    const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_published).count();
    if (!coop_page_rule::fresh(static_cast<uint64_t>(ageMs))) g_rows.clear();
    return g_rows;
}

}  // namespace coop_page_view
