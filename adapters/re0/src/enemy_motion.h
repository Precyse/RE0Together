#pragma once
#include <cstdint>

#include "enemy_puppet_rule.h"

// An enemy's current motion (uModel's motion block, game.h): read it for the snapshot, play the owner's on a puppet.
namespace enemy_motion {

struct State {
    uint16_t motion = 0;
    float frame = 0.0f;
};

// SEH-guarded read; false when the block is unreadable.
bool read(uintptr_t enemy, State& out);

// Game thread: makes the enemy play `motion` at `frame` when it differs from what it plays (another number through
// the game's own setter, the same one only when its frame drifted).
void play(uintptr_t enemy, uint16_t motion, float frame);

}  // namespace enemy_motion
