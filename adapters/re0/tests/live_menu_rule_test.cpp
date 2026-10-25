// Checks which local screens keep the world running and when a hurt character stops it again (no game). Exit 0 when
// every check passes.
#include <cstdint>
#include <cstdio>

#include "../src/live_menu_rule.h"

namespace {

using live_menu_rule::HurtWatch;
using live_menu_rule::Screen;
using live_menu_rule::screenOf;
using live_menu_rule::View;
namespace phase = room_phase;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// A view of `p` pushed over plain gameplay, with no submenu, no pause menu and no scripted event.
View over(int32_t p, int32_t beneath = phase::Main) { return View{p, beneath, false, false, false}; }

View withSubMenu(View view) {
    view.subMenuOpen = true;
    return view;
}

View withPause(View view) {
    view.paused = true;
    return view;
}

View inScriptEvent(View view) {
    view.scriptEvent = true;
    return view;
}

void testScreens() {
    check(screenOf(withSubMenu(over(phase::SubScreen)), true) == Screen::Live, "the inventory is live with a peer");
    check(screenOf(over(phase::Map), true) == Screen::Live, "the map is live with a peer");
    check(screenOf(withSubMenu(over(phase::SubScreen)), false) == Screen::Holding, "the inventory holds without live menus");
    check(screenOf(over(phase::Map), false) == Screen::Holding, "the map holds without live menus");
    check(screenOf(withPause(withSubMenu(over(phase::SubScreen))), true) == Screen::Holding, "the pause menu holds over a live menu");
    check(screenOf(withPause(over(phase::Main, room_phase::kUnreadable)), true) == Screen::Holding, "the pause menu holds");
    check(screenOf(over(phase::Save), true) == Screen::Holding, "the save screen holds");
    check(screenOf(over(phase::EventDemo), true) == Screen::Holding, "a cutscene holds");
    check(screenOf(over(phase::Movie), true) == Screen::Holding, "a movie holds");
    check(screenOf(over(phase::Option), true) == Screen::Holding, "the option phase holds");
    check(screenOf(over(phase::Main, room_phase::kUnreadable), true) == Screen::None, "plain gameplay is no screen");
    check(screenOf(over(phase::DoorLoad), true) == Screen::None, "a door is no screen");
    check(screenOf(over(phase::Event), true) == Screen::None, "a scripted event is no screen");
}

void testMessages() {
    check(screenOf(over(phase::Message), true) == Screen::Live, "an examine message over gameplay is live with a peer");
    check(screenOf(over(phase::MessageImm), true) == Screen::Live, "the cannot-change notice over gameplay is live");
    check(screenOf(over(phase::Message), false) == Screen::Holding, "a message holds without live menus");
    check(screenOf(over(phase::Message, phase::Event), true) == Screen::Holding, "an event's message holds");
    check(screenOf(over(phase::Message, room_phase::kUnreadable), true) == Screen::Holding, "a message over nothing known holds");
    check(screenOf(inScriptEvent(over(phase::Message)), true) == Screen::Holding,
          "a message under a scripted event's pause bits holds");
    check(screenOf(withPause(over(phase::Message)), true) == Screen::Holding, "the pause menu holds over a message");
}

void testStops() {
    using live_menu_rule::bit;
    using live_menu_rule::screenStops;
    check(screenStops(phase::SubScreen) == (bit(7) | bit(8)), "the inventory stops with reasons 7 and 8");
    check(screenStops(phase::Map) == (bit(7) | bit(8)), "the map stops with reasons 7 and 8");
    check(screenStops(phase::Message) == (bit(7) | bit(12)), "a message stops with reasons 7 and 12");
    check(screenStops(phase::MessageImm) == bit(9), "the notice stops with reason 9");
    check(screenStops(phase::Main) == 0 && screenStops(phase::Save) == 0, "other phases are never released");
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
    testMessages();
    testStops();
    testHurtStopsTheWorld();
    testHoldingNeverReleases();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
