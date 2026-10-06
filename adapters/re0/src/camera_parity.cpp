#include "camera_parity.h"

#include <chrono>

#include "character_owner.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "room_phase.h"

namespace {

using character_owner::Character;
using character_owner::SwitchResult;
using character_owner::switchTo;
using Clock = std::chrono::steady_clock;

constexpr auto kMinSwapInterval = std::chrono::seconds(1);

Clock::time_point g_lastSwap;

// With a peer the camera stays on this machine's own character, in every party mode: after a save load, a script's
// switch or a gamepad's switch button (which the keyboard hiding in command_input does not cover), once the screen
// is settled. Menus, doors and cutscenes (any room phase but Main) run on whatever the game set, then the camera
// comes back.
void onTick() {
    if (!net_pad::active()) return;
    const auto now = Clock::now();
    const Character own = character_owner::localCharacter();
    if (own == Character::Unknown || game_state::menuOpen() || game_state::doorActive() ||
        game_state::roomPhase() != room_phase::Main || now - g_lastSwap < kMinSwapInterval) {
        return;
    }
    if (switchTo(own) != SwitchResult::Done) return;
    g_lastSwap = now;
    logger::write("camera_parity: focus back on %s", character_owner::name(own));
}

}  // namespace

namespace camera_parity {

void enable() { game_tick::addCallback("camera_parity", onTick); }

}  // namespace camera_parity
