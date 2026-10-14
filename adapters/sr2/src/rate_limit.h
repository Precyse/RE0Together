#pragma once
#include <cstdint>

// A log line that may be due every frame is written at most once per interval (pure, unit tested), and says how many
// triggers it stands for.
namespace rate_limit {

constexpr uint64_t kLogIntervalMs = 1000;

class RateLimit {
public:
    // True when a line may be written at `nowMs`; `triggers` then holds the triggers since the last line (this one
    // included). A trigger that may not be written is only counted.
    bool allow(uint64_t nowMs, uint32_t& triggers) {
        ++pending_;
        if (written_ && nowMs - lastMs_ < kLogIntervalMs) return false;
        written_ = true;
        lastMs_ = nowMs;
        triggers = pending_;
        pending_ = 0;
        return true;
    }

private:
    bool written_ = false;
    uint64_t lastMs_ = 0;
    uint32_t pending_ = 0;
};

}  // namespace rate_limit
