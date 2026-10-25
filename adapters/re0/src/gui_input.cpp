#include "gui_input.h"

#include "game.h"

namespace {

using CdeclBits = uint32_t(__cdecl*)();
using CdeclBool = bool(__cdecl*)();
using CdeclPosition = float*(__cdecl*)(float* out);
using CdeclSound = void(__cdecl*)(int32_t id);

constexpr uint32_t kHitTestPrecise = 1;

}  // namespace

namespace gui_input {

uint32_t trigger() { return reinterpret_cast<CdeclBits>(game::kMenuTriggerFunction)(); }

uint32_t repeat() { return reinterpret_cast<CdeclBits>(game::kMenuRepeatFunction)(); }

uint32_t padMask(uintptr_t offset) {
    const uintptr_t pad = game::readPointer(game::kGamePadGlobal);
    uint32_t mask = 0;
    if (pad) game::readMemory(pad + offset, mask);
    return mask;
}

bool mouseClicked(float (&position)[kPositionFloats]) {
    if (!reinterpret_cast<CdeclBool>(game::kMouseClickedFunction)()) return false;
    reinterpret_cast<CdeclPosition>(game::kMousePositionFunction)(position);
    return true;
}

bool hitTest(uintptr_t gui, uintptr_t instance, uintptr_t object, float (&position)[kPositionFloats]) {
    return gui && instance && object &&
           game::callThiscall<bool>(game::kGuiHitTestFunction, reinterpret_cast<void*>(gui), instance, object, position, kHitTestPrecise);
}

void playSound(int32_t id) { reinterpret_cast<CdeclSound>(game::kPlayUiSoundFunction)(id); }

}  // namespace gui_input
