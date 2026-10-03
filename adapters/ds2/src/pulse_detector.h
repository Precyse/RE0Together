#pragma once
// Finds the one-shot edges in a stream of reported boolean variables (pure logic, unit tested). A variable that leaves
// its resting value and comes back within `kPulseWindowUs` (a stumble trigger, a throw, a pickup) is a pulse: the
// unreliable ANIM_STATE may lose either edge, and the once-a-second snapshot can no longer tell, so a pulse is also
// sent as a reliable ANIM_EVENT.
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "time_us.h"

class PulseDetector {
public:
    static constexpr TimeUs kPulseWindowUs = 500'000;

    // The latest reported value of boolean variable `index` at time `now`. When this report ends a pulse (the variable
    // went back after at most the window), returns the value it held during the pulse.
    std::optional<bool> onBool(uint16_t index, bool value, TimeUs now) {
        State& state = states_[index];
        if (!state.seen) {
            state = {true, false, value, now};
            return std::nullopt;
        }
        if (value == state.value) return std::nullopt;
        const bool wasPulse = state.changedBefore && now - state.changedAt <= kPulseWindowUs;
        const bool heldDuring = state.value;
        state.value = value;
        state.changedAt = now;
        state.changedBefore = true;
        if (wasPulse) return heldDuring;
        return std::nullopt;
    }

    void clear() { states_.clear(); }

private:
    struct State {
        bool seen = false;
        bool changedBefore = false;  // the value was seen to change at least once, so `changedAt` is meaningful
        bool value = false;
        TimeUs changedAt = 0;
    };

    std::unordered_map<uint16_t, State> states_;
};
