// Checks how long a peer's open screen holds the world (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/menu_hold_rule.h"

namespace {

using menu_hold_rule::Clock;
using menu_hold_rule::PeerMenu;
using menu_hold_rule::holds;
using menu_hold_rule::isOpen;
using menu_hold_rule::observe;
namespace phase = room_phase;

constexpr Clock::time_point kStart{};
int g_failures = 0;

Clock::time_point at(int seconds) { return kStart + std::chrono::seconds(seconds); }

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testMenuReleasesAfterCap() {
    PeerMenu menu;
    for (int second = 0; second <= 30; second += 2) observe(menu, true, phase::SubScreen, at(second));
    check(holds(menu, at(10)) && isOpen(menu, at(10)), "a fresh menu holds");
    check(!holds(menu, at(21)), "a menu open past the cap releases");
    check(isOpen(menu, at(30)), "a released menu still counts as open");
}

void testReadingNeverReleases() {
    PeerMenu menu;
    for (int second = 0; second <= 60; second += 2) observe(menu, true, phase::Message, at(second));
    check(holds(menu, at(60)), "reading holds past the cap");
    PeerMenu cutscene;
    for (int second = 0; second <= 60; second += 2) observe(cutscene, true, phase::EventDemo, at(second));
    check(holds(cutscene, at(60)), "a cutscene holds past the cap");
}

void testCloseAndStale() {
    PeerMenu menu;
    observe(menu, true, phase::Option, at(0));
    observe(menu, false, phase::Main, at(1));
    check(!holds(menu, at(1)), "closing releases at once");
    observe(menu, true, phase::Option, at(2));
    check(!isOpen(menu, at(9)), "a menu that stopped refreshing expires");
}

void testReopenRestartsCap() {
    PeerMenu menu;
    observe(menu, true, phase::SubScreen, at(0));
    observe(menu, false, phase::Main, at(15));
    for (int second = 16; second <= 30; second += 2) observe(menu, true, phase::SubScreen, at(second));
    check(holds(menu, at(30)), "a reopened menu gets a new cap");
}

}  // namespace

int main() {
    testMenuReleasesAfterCap();
    testReadingNeverReleases();
    testCloseAndStale();
    testReopenRestartsCap();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
