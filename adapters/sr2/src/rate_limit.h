#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>

// A log line that may be due every frame is written at most once per interval (one second unless the limit is made with
// another; pure, unit tested), and says how many triggers it stands for.
namespace rate_limit {

constexpr uint64_t kLogIntervalMs = 1000;
constexpr size_t kMaxOnceKeys = 256;

class RateLimit {
public:
    explicit RateLimit(uint64_t intervalMs = kLogIntervalMs) : intervalMs_(intervalMs) {}

    // True when a line may be written at `nowMs`; `triggers` then holds the triggers since the last line (this one
    // included). A trigger that may not be written is only counted.
    bool allow(uint64_t nowMs, uint32_t& triggers) {
        ++pending_;
        if (written_ && nowMs - lastMs_ < intervalMs_) return false;
        written_ = true;
        lastMs_ = nowMs;
        triggers = pending_;
        pending_ = 0;
        return true;
    }

private:
    uint64_t intervalMs_;
    bool written_ = false;
    uint64_t lastMs_ = 0;
    uint32_t pending_ = 0;
};

// A line written once per key (pure, unit tested). Bounded: when it is full, the oldest key is forgotten.
class OncePerKey {
public:
    // True the first time `key` is asked about.
    bool firstTime(uint64_t key) {
        if (std::find(keys_.begin(), keys_.end(), key) != keys_.end()) return false;
        if (keys_.size() == kMaxOnceKeys) keys_.pop_front();
        keys_.push_back(key);
        return true;
    }

private:
    std::deque<uint64_t> keys_;
};

}  // namespace rate_limit
