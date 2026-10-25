#pragma once
#include <cstdint>

// Menu input and sounds as the game's own screens read and play them (game.h "Menu input"): the merged trigger and
// auto-repeat bits, the pad's confirm and cancel masks, the mouse click and position, the hit test of a touch polygon,
// and the UI sounds. Game thread.
namespace gui_input {

constexpr size_t kPositionFloats = 4;

uint32_t trigger();
uint32_t repeat();

// The sGamePad trigger mask at `offset` (game::kPadDecideMaskOffset, game::kPadCancelMaskOffset), 0 when unreadable.
uint32_t padMask(uintptr_t offset);

// The left button was pressed this frame; `position` then holds the cursor in screen units.
bool mouseClicked(float (&position)[kPositionFloats]);

// The cursor at `position` is on `object` (a touch polygon) of `instance` in the uGUI `gui`.
bool hitTest(uintptr_t gui, uintptr_t instance, uintptr_t object, float (&position)[kPositionFloats]);

void playSound(int32_t id);

}  // namespace gui_input
