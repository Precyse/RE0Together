#pragma once
#include <cstdint>

#include "slow_frame_rule.h"

// SLOW_FRAME diagnostics (partner_mode=state): game_tick times its callbacks, move scopes and post-move steps per
// category and, when the time between two ticks of the controlled player's move reaches 50 ms, writes one log line
// `SLOW_FRAME 83ms: net 1 puppet 4 enemies 2 events 0 fx 0 other 1` (at most one per 5 s). Off until enable().
namespace slow_frame {

// Turns the timing on. Call from state_mode::enableAll, before game_tick::install.
void enable();

// Game thread, first thing in a controlled-player tick: closes the previous frame and logs it when it was slow.
void beginFrame();

// Adds the lifetime of the object to `category` for the current frame; free when the timing is off.
class Scope {
public:
    explicit Scope(Category category);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Category category_;
    uint64_t startUs_;
};

}  // namespace slow_frame
