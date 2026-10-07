#pragma once
#include <cstdint>

#include "position_blend.h"

// Pure rules of an enemy that the peer owns and this machine also runs (no game access, unit tested). The enemy moves
// by its own AI; a snapshot only measures how far it is from the owner's, and that error is removed over the next
// ticks on top of its own movement. HP from a snapshot is only a backstop for damage no hit event explains.
namespace enemy_follow_rule {

constexpr float kDeadZone = 12.0f;        // smaller errors are left alone
constexpr float kSnapDistance = 300.0f;   // a bigger error is a teleport, not drift
constexpr float kSharePerTick = 0.2f;     // share of the remaining error removed each tick
constexpr int64_t kHitSettleMs = 500;     // a snapshot this soon after a replayed hit may predate it
constexpr int64_t kDeathFallbackMs = 1000;  // a death no hit explained, applied once the owner has held it this long
constexpr int64_t kNever = INT64_MIN / 2;

enum class Start { None, Correct, Snap };

struct Correction {
    float remaining[3] = {};
};

// On a snapshot: the error from `local` to `owner`, measured once.
inline Start start(Correction& correction, const float (&local)[3], const float (&owner)[3]) {
    const float drift = position_blend::distance(local, owner);
    const bool correct = drift >= kDeadZone && drift <= kSnapDistance;
    for (int i = 0; i < 3; ++i) correction.remaining[i] = correct ? owner[i] - local[i] : 0.0f;
    if (drift < kDeadZone) return Start::None;
    return correct ? Start::Correct : Start::Snap;
}

// Each tick: the part of the remaining error to add to the enemy's position now.
inline void step(Correction& correction, float (&delta)[3]) {
    for (int i = 0; i < 3; ++i) {
        delta[i] = correction.remaining[i] * kSharePerTick;
        correction.remaining[i] -= delta[i];
    }
}

class HpBackstop {
public:
    void onHitReplayed(int64_t nowMs) { lastHitMs_ = nowMs; }

    // The owner's HP from a snapshot: true when it should be written into the local enemy. Never revives a local dead
    // enemy, and a lethal value waits so the death comes from the replayed hit (its reaction and crit) when there is one.
    bool applies(int32_t local, int32_t owner, int64_t nowMs) {
        if (owner > 0) {
            ownerDeadSinceMs_ = kNever;
        } else if (ownerDeadSinceMs_ == kNever) {
            ownerDeadSinceMs_ = nowMs;
        }
        if (local == owner || local <= 0 || nowMs - lastHitMs_ < kHitSettleMs) return false;
        return owner > 0 || nowMs - ownerDeadSinceMs_ >= kDeathFallbackMs;
    }

private:
    int64_t lastHitMs_ = kNever;
    int64_t ownerDeadSinceMs_ = kNever;
};

}  // namespace enemy_follow_rule
