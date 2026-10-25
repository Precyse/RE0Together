#include "live_menus.h"

#include <atomic>

#include "character_owner.h"
#include "game.h"
#include "game_state.h"
#include "log.h"
#include "net_pad.h"
#include "room_phase.h"

namespace {

using live_menu_rule::Screen;

std::atomic<bool> g_on{false};
live_menu_rule::HurtWatch g_hurt;  // game thread
bool g_releasing = false;          // game thread: the stop reasons were cleared last frame

void setStop(uint32_t reason, bool on) {
    void* units = reinterpret_cast<void*>(game::readPointer(game::kUnitSystemGlobal));
    if (units) game::callThiscall<void>(game::kSetStopReasonFunction, units, reason, static_cast<uint32_t>(on));
}

// Sets or clears every stop reason in `stops` (live_menu_rule::screenStops).
void setScreenStops(uint32_t stops, bool on) {
    for (uint32_t reason = 0; stops >> reason != 0; ++reason) {
        if (stops >> reason & 1) setStop(reason, on);
    }
}

bool anyStopped(uint32_t stops) {
    for (uint32_t reason = 0; stops >> reason != 0; ++reason) {
        if ((stops >> reason & 1) && game_state::unitsStopped(reason)) return true;
    }
    return false;
}

}  // namespace

namespace live_menus {

void configure(bool on) {
    if (on && !game::bytesMatch(game::kSetStopReasonFunction, game::kSetStopReasonBytes)) {
        logger::write("live_menus: unexpected bytes at the stop reason setter, menus pause the world");
        on = false;
    }
    g_on = on;
    logger::write("live_menus: %s", on ? "on" : "off");
}

Screen localScreen() {
    const int32_t phase = game_state::roomPhase();
    const live_menu_rule::View view{phase, game_state::phaseBeneath(), game_state::menuOpen(), game_state::paused(),
                                    game_state::scriptEventPaused()};
    return live_menu_rule::screenOf(view, g_on && net_pad::active());
}

void onUnitUpdate() {
    const Screen screen = localScreen();
    int32_t hp = 0;
    const uintptr_t player = character_owner::localPlayer();
    const bool known = player && game::readMemory(player + game::kPlayerHpOffset, hp);
    g_hurt.update(known ? screen : Screen::None, hp);
    const bool release = g_hurt.releases();
    const int32_t phase = game_state::roomPhase();
    const uint32_t stops = live_menu_rule::screenStops(phase);
    if (release) {
        if (!g_releasing) logger::write("live_menus: %s open, the world keeps running", room_phase::name(phase));
        if (anyStopped(stops)) setScreenStops(stops, false);
    } else if (g_releasing && screen == Screen::Live) {
        setScreenStops(stops, true);
        logger::write("live_menus: hurt in the %s, the world waits until it closes", room_phase::name(phase));
    }
    g_releasing = release;
}

bool inputHeld() { return g_releasing; }

}  // namespace live_menus
