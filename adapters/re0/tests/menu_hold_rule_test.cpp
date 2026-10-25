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
using live_menu_rule::Screen;

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
    for (int second = 0; second <= 30; second += 2) observe(menu, Screen::Holding, phase::SubScreen, at(second));
    check(holds(menu, at(10)) && isOpen(menu, at(10)), "a fresh menu holds");
    check(!holds(menu, at(21)), "a menu open past the cap releases");
    check(isOpen(menu, at(30)), "a released menu still counts as open");
}

void testReadingNeverReleases() {
    PeerMenu menu;
    for (int second = 0; second <= 60; second += 2) observe(menu, Screen::Holding, phase::Message, at(second));
    check(holds(menu, at(60)), "reading holds past the cap");
    PeerMenu cutscene;
    for (int second = 0; second <= 60; second += 2) observe(cutscene, Screen::Holding, phase::EventDemo, at(second));
    check(holds(cutscene, at(60)), "a cutscene holds past the cap");
}

void testCloseAndStale() {
    PeerMenu menu;
    observe(menu, Screen::Holding, phase::Option, at(0));
    observe(menu, Screen::None, phase::Main, at(1));
    check(!holds(menu, at(1)), "closing releases at once");
    observe(menu, Screen::Holding, phase::Option, at(2));
    check(!isOpen(menu, at(9)), "a menu that stopped refreshing expires");
}

void testReopenRestartsCap() {
    PeerMenu menu;
    observe(menu, Screen::Holding, phase::SubScreen, at(0));
    observe(menu, Screen::None, phase::Main, at(15));
    for (int second = 16; second <= 30; second += 2) observe(menu, Screen::Holding, phase::SubScreen, at(second));
    check(holds(menu, at(30)), "a reopened menu gets a new cap");
}

void testLiveMenuNeverHolds() {
    PeerMenu menu;
    observe(menu, Screen::Live, phase::SubScreen, at(0));
    check(!holds(menu, at(0)) && isOpen(menu, at(0)), "a live inventory is open and does not hold");
    observe(menu, Screen::Live, phase::Map, at(2));
    check(!holds(menu, at(2)), "a live map does not hold");
}

void testHoldingAfterLiveGetsTheFullCap() {
    PeerMenu menu;
    for (int second = 0; second <= 18; second += 2) observe(menu, Screen::Live, phase::SubScreen, at(second));
    for (int second = 19; second <= 35; second += 2) observe(menu, Screen::Holding, phase::Main, at(second));
    check(holds(menu, at(35)), "a pause after a live menu holds for its own cap");
    check(!holds(menu, at(40)), "the pause releases at its cap");
}

}  // namespace

int main() {
    testMenuReleasesAfterCap();
    testReadingNeverReleases();
    testCloseAndStale();
    testReopenRestartsCap();
    testLiveMenuNeverHolds();
    testHoldingAfterLiveGetsTheFullCap();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
