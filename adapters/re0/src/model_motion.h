#pragma once
#include <cstdint>

#include "motion_rule.h"

// A model's current motion (uModel's motion block, game.h): read it for a snapshot, play the owner's on a copy. Used
// for enemy puppets and for remote-owned characters.
namespace model_motion {

struct State {
    uint16_t motion = 0;
    float frame = 0.0f;
};

// SEH-guarded read; false when the block is unreadable.
bool read(uintptr_t model, State& out);

// What `play` would do for this target, without doing it.
motion_rule::Step stepFor(uintptr_t model, uint16_t motion, float frame);

// Game thread: applies `step` toward `motion` at `frame` (another number through the game's own setter, the same
// one only a frame write).
void apply(uintptr_t model, motion_rule::Step step, uint16_t motion, float frame);

// Game thread: stepFor + apply.
void play(uintptr_t model, uint16_t motion, float frame);

}  // namespace model_motion
