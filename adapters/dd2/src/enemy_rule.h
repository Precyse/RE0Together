#pragma once
// The enemy sync's rules (pure, unit tested; DD2 design world.md 1-2): which host enemies are streamed to a guest, and
// which hits from a guest the host replays (the DS2 limits: at most 20 hits per target and 120 per sender each second,
// damage finite, positive and at most the target's full HP).
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <utility>

#include "roster_rule.h"

namespace enemy_rule {

constexpr double kStreamRadiusM = 120.0;  // farther from every guest than this, an enemy is not streamed
constexpr uint32_t kHitsPerTarget = 20;
constexpr uint32_t kHitsPerSender = 120;
constexpr uint64_t kBudgetWindowMs = 1000;

struct Point {
    double x = 0, y = 0, z = 0;
};

inline bool streamed(const Point& enemy, std::span<const Point> guests) {
    for (const Point& guest : guests) {
        const double dx = enemy.x - guest.x, dy = enemy.y - guest.y, dz = enemy.z - guest.z;
        if (dx * dx + dy * dy + dz * dz <= kStreamRadiusM * kStreamRadiusM) return true;
    }
    return false;
}

inline bool damageOk(float damage, float targetMaxHp) {
    return std::isfinite(damage) && damage > 0.0f && std::isfinite(targetMaxHp) && damage <= targetMaxHp;
}

// Counts hits in one-second windows per sender and per (sender, target).
class HitBudget {
public:
    bool admit(uint8_t sender, const roster_rule::Key& target, uint64_t nowMs) {
        if (nowMs - windowStartMs_ >= kBudgetWindowMs) {
            windowStartMs_ = nowMs;
            perSender_.clear();
            perTarget_.clear();
        }
        uint32_t& bySender = perSender_[sender];
        uint32_t& byTarget = perTarget_[{sender, target}];
        if (bySender >= kHitsPerSender || byTarget >= kHitsPerTarget) return false;
        ++bySender;
        ++byTarget;
        return true;
    }

private:
    uint64_t windowStartMs_ = 0;
    std::map<uint8_t, uint32_t> perSender_;
    std::map<std::pair<uint8_t, roster_rule::Key>, uint32_t> perTarget_;
};

}  // namespace enemy_rule
