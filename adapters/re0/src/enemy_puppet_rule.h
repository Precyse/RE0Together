#pragma once
#include <cstdint>

#include "position_blend.h"

// Pure rules of an enemy that this machine does not own (a puppet): its pose follows the owner's snapshots. No game
// access, unit tested.
namespace enemy_puppet_rule {

constexpr float kSnapDistance = 300.0f;  // a bigger gap is a teleport, not lag

// What the owner last said about one enemy, with the velocity between its last two snapshots.
struct Track {
    bool valid = false;
    int32_t hp = 0;
    float pos[3] = {};
    float quat[4] = {};
    float velocity[3] = {};
    int64_t stateMs = 0;
};

// Folds a received snapshot into the track.
inline void observe(Track& track, const float (&pos)[3], const float (&quat)[4], int32_t hp, int64_t nowMs) {
    if (track.valid) {
        position_blend::velocity(track.pos, pos, static_cast<float>(nowMs - track.stateMs) / 1000.0f, track.velocity);
    } else {
        for (float& v : track.velocity) v = 0.0f;
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

enum class Step { Hold, Blend, Snap };

inline Step stepFor(float drift) {
    if (drift < position_blend::kDeadZone) return Step::Hold;
    return drift <= kSnapDistance ? Step::Blend : Step::Snap;
}

}  // namespace enemy_puppet_rule
