#include "model_motion.h"

#include <windows.h>

#include "game.h"

namespace {

using motion_rule::Step;

uintptr_t blockOf(uintptr_t model) { return model + game::kModelMotionBlockOffset; }

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

namespace model_motion {

bool read(uintptr_t model, State& out) {
    const uintptr_t block = blockOf(model);
    return game::readMemory(block + game::kMotionNumberOffset, out.motion) &&
           game::readMemory(block + game::kMotionFrameOffset, out.frame);
}

Step stepFor(uintptr_t model, uint16_t motion, float frame) {
    State local;
    if (!read(model, local)) return Step::Keep;
    return motion_rule::stepFor(local.motion, local.frame, motion, frame);
}

void apply(uintptr_t model, Step step, uint16_t motion, float frame) {
    if (step == Step::Keep) return;
    const uintptr_t block = blockOf(model);
    if (step == Step::SetMotion && !setMotion(block, motion)) return;
    setFrame(block, frame);
}

void play(uintptr_t model, uint16_t motion, float frame) { apply(model, stepFor(model, motion, frame), motion, frame); }

}  // namespace model_motion
