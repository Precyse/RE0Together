#pragma once
#include <cstdint>

#include "position_blend.h"

// Pure rules of an enemy that this machine does not own (a puppet): it follows the owner's snapshots and decides
// nothing itself. No game access, unit tested.
namespace enemy_puppet_rule {

constexpr int64_t kStateFreshMs = 500;     // a snapshot older than this no longer holds the enemy still
constexpr int64_t kReactionMs = 700;       // after the owner's HP dropped the update runs so the hit reaction plays
constexpr int64_t kNeverMs = -1000000000;  // "no HP drop seen yet"
constexpr float kSnapDistance = 300.0f;    // a bigger gap is a teleport, not lag

// What the owner last said about one enemy, with the velocity between its last two snapshots.
struct Track {
    bool valid = false;
    int32_t hp = 0;
    float pos[3] = {};
    float quat[4] = {};
    float velocity[3] = {};
    int64_t stateMs = 0;
    int64_t hpDropMs = kNeverMs;
};

// Folds a received snapshot into the track.
inline void observe(Track& track, const float (&pos)[3], const float (&quat)[4], int32_t hp, int64_t nowMs) {
    if (track.valid) {
        position_blend::velocity(track.pos, pos, static_cast<float>(nowMs - track.stateMs) / 1000.0f, track.velocity);
        if (hp < track.hp) track.hpDropMs = nowMs;
    } else {
        for (float& v : track.velocity) v = 0.0f;
        track.hpDropMs = kNeverMs;
    }
    for (int i = 0; i < 3; ++i) track.pos[i] = pos[i];
    for (int i = 0; i < 4; ++i) track.quat[i] = quat[i];
    track.hp = hp;
    track.stateMs = nowMs;
    track.valid = true;
}

// Where the enemy should be now: the last position advanced by its velocity (capped).
inline void aim(const Track& track, int64_t nowMs, float (&out)[3]) {
    position_blend::extrapolate(track.pos, track.velocity, static_cast<float>(nowMs - track.stateMs) / 1000.0f, out);
}

// The owner's enemy is alive, freshly reported and not reacting to a hit: its local update (AI) is skipped, so it
// cannot decide anything and only the snapshots move it.
inline bool skipsUpdate(const Track& track, int64_t nowMs) {
    return track.valid && track.hp > 0 && nowMs - track.stateMs <= kStateFreshMs &&
           nowMs - track.hpDropMs > kReactionMs;
}

enum class Step { Hold, Blend, Snap };

inline Step stepFor(float drift) {
    if (drift < position_blend::kDeadZone) return Step::Hold;
    return drift <= kSnapDistance ? Step::Blend : Step::Snap;
}

}  // namespace enemy_puppet_rule
