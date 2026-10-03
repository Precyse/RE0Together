#pragma once
// The adapter's one clock for network timing: microseconds on this machine's monotonic clock. Timestamps on the wire
// are the sender's TimeUs; clock_sync converts a peer's into this machine's.
#include <chrono>
#include <cstdint>

using TimeUs = int64_t;

inline TimeUs nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
