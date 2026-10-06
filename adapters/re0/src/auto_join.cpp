#include "auto_join.h"

#include <chrono>

#include "character_owner.h"
#include "game.h"
#include "game_state.h"
#include "log.h"
#include "net_pad.h"
#include "room_phase.h"
#include "save_redirect.h"
#include "session_slot.h"
#include "virtual_keys.h"

namespace {

using Clock = std::chrono::steady_clock;

// Long enough for each logo, notice and menu to accept input, short enough that joining feels direct.
constexpr auto kConfirmInterval = std::chrono::milliseconds(2500);
// Enter alone cannot pass every screen (an empty save slot under the load cursor ignores it): after this many presses
// without reaching the game the guest gets the keyboard back and picks a slot; the load is redirected to the host's slot.
constexpr int kMaxConfirms = 6;

enum class Mode { Idle, Waiting, Confirming };

Mode g_mode = Mode::Idle;  // net thread only
Clock::time_point g_lastConfirm;
int g_confirms = 0;

// Outside gameplay the guest follows the host into its game; at game over it waits for the host's choice.
Mode modeNow() {
    const bool guest = net_pad::active() && !character_owner::isHost() && save_redirect::servingSession() &&
                       session_slot::current() != session_slot::kUnknown;
    if (!guest) return Mode::Idle;
    const bool outside = game_state::roomPhase() == room_phase::Dead || game::controlled() == 0;
    if (!outside) return Mode::Idle;
    return session_slot::hostInGame() ? Mode::Confirming : Mode::Waiting;
}

const char* describe(Mode mode) {
    switch (mode) {
        case Mode::Waiting: return "auto_join: waiting for the host to be in game";
        case Mode::Confirming: return "auto_join: taking the guest into the host's game";
        case Mode::Idle: break;
    }
    return "auto_join: in game";
}

}  // namespace

namespace auto_join {

void onNetTick() {
    const Mode mode = modeNow();
    if (mode != g_mode) {
        g_mode = mode;
        g_confirms = 0;
        virtual_keys::setRealKeyboardMuted(mode != Mode::Idle);
        logger::write("%s", describe(mode));
    }
    if (mode != Mode::Confirming || g_confirms >= kMaxConfirms) return;
    const auto now = Clock::now();
    if (now - g_lastConfirm < kConfirmInterval) return;
    g_lastConfirm = now;
    virtual_keys::tap(virtual_keys::kEnter);
    if (++g_confirms < kMaxConfirms) return;
    virtual_keys::setRealKeyboardMuted(false);
    logger::write("auto_join: no progress after %d presses, the keyboard is the guest's again (any slot loads the host's)", kMaxConfirms);
}

}  // namespace auto_join
