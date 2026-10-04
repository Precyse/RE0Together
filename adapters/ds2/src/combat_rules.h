#pragma once
// The pure rules of enemy combat (no game, unit tested): how many hits a peer may land per second, and which enemy
// deaths were already handled.
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "time_us.h"

namespace combat_rules {

// A peer's hits are accepted up to a total and a per-enemy count in each one-second window.
class HitLimiter {
public:
    static constexpr TimeUs kWindowUs = 1'000'000;
    static constexpr uint32_t kMaxPerEnemy = 20;
    static constexpr uint32_t kMaxTotal = 120;

    // True when this hit, aimed at `enemyKey`, fits in the current window (and is then counted).
    bool allow(uint16_t enemyKey, TimeUs now) {
        if (now - windowStart_ >= kWindowUs || now < windowStart_) {
            windowStart_ = now;
            total_ = 0;
            perEnemy_.clear();
        }
        uint32_t& enemyCount = perEnemy_[enemyKey];
        if (total_ >= kMaxTotal || enemyCount >= kMaxPerEnemy) return false;
        ++total_;
        ++enemyCount;
        return true;
    }

private:
    TimeUs windowStart_ = 0;
    uint32_t total_ = 0;
    std::unordered_map<uint16_t, uint32_t> perEnemy_;
};

// Enemy deaths seen so far, by net id: a death is handled once however often it is reported.
class DeathLedger {
public:
    static constexpr size_t kMaxEntries = 4096;

    // True the first time `netId` is marked; false for every repeat.
    bool markFirst(uint16_t netId) {
        if (dead_.size() >= kMaxEntries) dead_.clear();
        return dead_.insert(netId).second;
    }

    void clear() { dead_.clear(); }

private:
    std::unordered_set<uint16_t> dead_;
};

}  // namespace combat_rules
