#pragma once

// The host's give/take menu: its own cargo and the guest's side by side; F7 opens and closes it, the arrow keys
// choose a piece and Enter moves it to the other side (cargo_transfer). Keyboard only, read while the game window is
// in front, so the game keeps its mouse; the game does not see the menu's keys while it is open.
namespace cargo_menu {

// dx12_hook draw callback part (render thread).
void draw(float width, float height);

// input_filter: while the menu is open its keys (arrows, Enter) are kept from the game.
bool claimsKey(unsigned virtualKey);

}  // namespace cargo_menu
