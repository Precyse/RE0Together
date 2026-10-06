#include "auto_join.h"

#include <chrono>

#include "game.h"
#include "game_state.h"
#include "log.h"
#include "net_pad.h"
#include "session_rule.h"
#include "session_slot.h"
#include "virtual_keys.h"

namespace {

using Clock = std::chrono::steady_clock;

// Long enough for each logo, notice and menu to accept input, short enough that joining feels direct.
constexpr auto kConfirmInterval = std::chrono::milliseconds(2500);
// Enter alone cannot pass every screen (an empty save slot under the load cursor ignores it): after this many presses
// without reaching the game the guest gets the keyboard back and picks a slot; the load is redirected to the host's slot.
constexpr int kMaxConfirms = 6;
// A host that never continues (quit, away) must not leave the guest without a keyboard.
constexpr auto kMaxWait = std::chrono::seconds(60);

using Mode = session_rule::JoinMode;

bool g_fullJoin = false;  // set once at start-up: also drive the boot, title and load screens
Mode g_mode = Mode::Idle;  // net thread only
Clock::time_point g_lastConfirm;
Clock::time_point g_modeSince;
int g_confirms = 0;

// Outside gameplay the guest follows the host into its game; at game over it waits for the host's choice.
Mode modeNow() {
    const bool guest = net_pad::active() && session_slot::guestSession() && session_slot::current() != session_slot::kUnknown;
    return session_rule::joinMode(guest, g_fullJoin, game_state::roomPhase(), game::controlled() != 0,
                                  session_slot::hostInGame());
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

void enable() { g_fullJoin = true; }

void onNetTick() {
    const Mode mode = modeNow();
    if (mode != g_mode) {
        g_mode = mode;
        g_confirms = 0;
        g_modeSince = Clock::now();
        virtual_keys::setRealKeyboardMuted(mode != Mode::Idle);
        logger::write("%s", describe(mode));
    }
    const auto now = Clock::now();
    if (mode == Mode::Waiting && now - g_modeSince >= kMaxWait && g_modeSince != Clock::time_point{}) {
        g_modeSince = {};
        virtual_keys::setRealKeyboardMuted(false);
        logger::write("auto_join: the host did not continue in %lld s, the keyboard is the guest's again", static_cast<long long>(kMaxWait.count()));
    }
    if (mode != Mode::Confirming || g_confirms >= kMaxConfirms) return;
    if (now - g_lastConfirm < kConfirmInterval) return;
    g_lastConfirm = now;
    virtual_keys::tap(virtual_keys::kEnter);
    if (++g_confirms < kMaxConfirms) return;
    virtual_keys::setRealKeyboardMuted(false);
    logger::write("auto_join: no progress after %d presses, the keyboard is the guest's again (any slot loads the host's)", kMaxConfirms);
}

}  // namespace auto_join
