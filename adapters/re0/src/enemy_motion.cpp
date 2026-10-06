#include "enemy_motion.h"

#include <windows.h>

#include "game.h"

namespace {

using enemy_puppet_rule::MotionStep;

uintptr_t blockOf(uintptr_t enemy) { return enemy + game::kEnemyMotionBlockOffset; }

void setFrame(uintptr_t block, float frame) {
    game::writeMemory(block + game::kMotionFrameOffset, frame);
    game::writeMemory(block + game::kMotionPreviousFrameOffset, frame);
}

void setMotionUnguarded(uintptr_t block, uint16_t motion) {
    game::callThiscall<void>(game::kMotionSetNumberFunction, reinterpret_cast<void*>(block),
                             static_cast<uint32_t>(motion));
}

bool setMotion(uintptr_t block, uint16_t motion) {
    __try {
        setMotionUnguarded(block, motion);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

namespace enemy_motion {

bool read(uintptr_t enemy, State& out) {
    const uintptr_t block = blockOf(enemy);
    return game::readMemory(block + game::kMotionNumberOffset, out.motion) &&
           game::readMemory(block + game::kMotionFrameOffset, out.frame);
}

void play(uintptr_t enemy, uint16_t motion, float frame) {
    State local;
    if (!read(enemy, local)) return;
    const MotionStep step = enemy_puppet_rule::motionStepFor(local.motion, local.frame, motion, frame);
    if (step == MotionStep::Keep) return;
    const uintptr_t block = blockOf(enemy);
    if (step == MotionStep::SetMotion && !setMotion(block, motion)) return;
    setFrame(block, frame);
}

}  // namespace enemy_motion
