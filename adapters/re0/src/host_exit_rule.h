#pragma once
#include <cstdint>

#include "room_phase.h"

// Pure rule of a guest leaving for the main menu (host_exit.cpp, unit tested): besides the host leaving the session, a
// host whose game is no longer in play (back at its title, closed or crashed: its presence says another place or stops)
// for kHostAwayMs while this guest plays.
namespace host_exit_rule {

constexpr uint32_t kHostAwayMs = 5000;

// The guest's own game is already leaving its world (its game over, the exit to the title): a request to leave is spent
// and the watch starts over with the next world.
constexpr bool leavingOnItsOwn(int32_t phase) { return phase == room_phase::Dead || phase == room_phase::Exit; }

class AwayWatch {
public:
    // One look while this guest plays: `hostInGame` is the host's fresh presence saying In game. True once, when the host
    // has been out of the game for kHostAwayMs without a break.
    bool hostLeftGame(bool hostInGame, uint32_t nowMs) {
        if (hostInGame) {
            awaySince_.reset = true;
            return false;
        }
        if (awaySince_.reset) awaySince_ = {false, nowMs};
        if (reported_ || nowMs - awaySince_.ms < kHostAwayMs) return false;
        reported_ = true;
        return true;
    }

    // This guest's world is leaving on its own (leavingOnItsOwn): the watch starts over.
    void clear() { *this = AwayWatch{}; }

private:
    struct Since {
        bool reset = true;
        uint32_t ms = 0;
    };
    Since awaySince_;
    bool reported_ = false;
};

}  // namespace host_exit_rule
