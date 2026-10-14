#pragma once
#include <cstdint>

// The pure rule of milestone 5i, C1 (unit tested): the log of a guard that may trigger every frame is limited to one line
// per interval, and the line says how many triggers it stands for.
namespace ambient_guard_rules {

constexpr uint64_t kLogIntervalMs = 1000;

class RateLimit {
public:
    // True when a line may be written at `nowMs`; `skipped` then holds the triggers since the last line (this one
    // included). A trigger that may not be written is only counted.
    bool allow(uint64_t nowMs, uint32_t& skipped) {
        ++pending_;
        if (written_ && nowMs - lastMs_ < kLogIntervalMs) return false;
        written_ = true;
        lastMs_ = nowMs;
        skipped = pending_;
        pending_ = 0;
        return true;
    }

private:
    bool written_ = false;
    uint64_t lastMs_ = 0;
    uint32_t pending_ = 0;
};

}  // namespace ambient_guard_rules
