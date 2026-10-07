#pragma once
#include <array>
#include <cstdint>

// A virtual keyboard layered on the game's DirectInput keyboard: the adapter can tap keys, and can mute the real
// keyboard so a player cannot steer menus while the adapter drives them.
namespace virtual_keys {

constexpr uint8_t kEnter = 0x1C;  // DIK_RETURN

// Called by the dinput8 proxy with the IDirectInput8 the game created: hooks keyboard device creation.
void onDirectInput(void* directInput);

// Presses `scancode` for the next few keyboard reads, then releases it.
void tap(uint8_t scancode);

// While muted the game reads only virtual keys.
void setRealKeyboardMuted(bool muted);

// True when the game's last keyboard read reported `scancode` (DIK) down, before hiding and muting. The game's keyboard
// is foreground-only (SetCooperativeLevel 0x16 at 0x807691), so this is false while another window has the focus.
bool realKeyDown(uint8_t scancode);

// The game never sees these keys (DIK scan codes; 0 = none) while they are set.
using HiddenKeys = std::array<uint8_t, 2>;
void setHiddenKeys(const HiddenKeys& scancodes);

}  // namespace virtual_keys
