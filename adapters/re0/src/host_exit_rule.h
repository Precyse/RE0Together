#pragma once
#include <cstdint>

// Pure rule of a guest leaving for the main menu (host_exit.cpp, unit tested): besides the host leaving the session, a
// host whose game is no longer in play (back at its title, closed or crashed: its presence says another place or stops)
// for kHostAwayMs while this guest plays.
namespace host_exit_rule {

constexpr uint32_t kHostAwayMs = 5000;

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

    // This guest is not in play (title, loading): the watch starts over.
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
