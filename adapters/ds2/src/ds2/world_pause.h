#pragma once
// DS2-internal: the world keeps running under the game's own menus while linked (see ds2/world_pause.cpp).
namespace world_pause {

// Start-up: the detour on the game module's IsPaused and the log of the pause menu opening and closing.
void installEarly();

// Whether the pause menu (Esc) is open: DSGameState bit 2. Any thread.
bool menuUp();

}  // namespace world_pause
