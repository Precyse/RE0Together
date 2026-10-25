// Checks which local screens keep the world running and when a hurt character stops it again (no game). Exit 0 when
// every check passes.
#include <cstdio>

#include "../src/live_menu_rule.h"

namespace {

using live_menu_rule::HurtWatch;
using live_menu_rule::Screen;
using live_menu_rule::screenOf;
namespace phase = room_phase;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testScreens() {
    check(screenOf(phase::SubScreen, true, false, true) == Screen::Live, "the inventory is live with a peer");
    check(screenOf(phase::Map, false, false, true) == Screen::Live, "the map is live with a peer");
    check(screenOf(phase::SubScreen, true, false, false) == Screen::Holding, "the inventory holds without live menus");
    check(screenOf(phase::Map, false, false, false) == Screen::Holding, "the map holds without live menus");
    check(screenOf(phase::SubScreen, true, true, true) == Screen::Holding, "the pause menu holds over a live menu");
    check(screenOf(phase::Main, false, true, true) == Screen::Holding, "the pause menu holds");
    check(screenOf(phase::Save, false, false, true) == Screen::Holding, "the save screen holds");
    check(screenOf(phase::Message, false, false, true) == Screen::Holding, "a reading screen holds");
    check(screenOf(phase::MessageImm, false, false, true) == Screen::Holding, "an immediate message holds");
    check(screenOf(phase::EventDemo, false, false, true) == Screen::Holding, "a cutscene holds");
    check(screenOf(phase::Movie, false, false, true) == Screen::Holding, "a movie holds");
    check(screenOf(phase::Option, false, false, true) == Screen::Holding, "the option phase holds");
    check(screenOf(phase::Main, false, false, true) == Screen::None, "plain gameplay is no screen");
    check(screenOf(phase::DoorLoad, false, false, true) == Screen::None, "a door is no screen");
    check(screenOf(phase::Event, false, false, true) == Screen::None, "a scripted event is no screen");
}

void testHurtStopsTheWorld() {
    HurtWatch watch;
    watch.update(Screen::Live, 100);
    check(watch.releases(), "a live menu releases the world");
    watch.update(Screen::Live, 100);
    watch.update(Screen::Live, 140);
    check(watch.releases(), "healing in the menu keeps it running");
    watch.update(Screen::Live, 120);
    check(!watch.releases(), "a hit stops the world");
    watch.update(Screen::Live, 140);
    check(!watch.releases(), "a hurt menu stays stopped until it closes");
    watch.update(Screen::None, 140);
    check(!watch.releases(), "no screen, nothing released");
    watch.update(Screen::Live, 140);
    check(watch.releases(), "the next menu runs again");
}

void testHoldingNeverReleases() {
    HurtWatch watch;
    watch.update(Screen::Holding, 100);
    check(!watch.releases(), "a holding screen is not released");
    watch.update(Screen::Live, 100);
    watch.update(Screen::Holding, 100);
    watch.update(Screen::Live, 50);
    check(watch.releases(), "the HP seen before a holding screen is not compared");
}

}  // namespace

int main() {
    testScreens();
    testHurtStopsTheWorld();
    testHoldingNeverReleases();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
