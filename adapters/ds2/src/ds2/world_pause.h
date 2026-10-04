#pragma once
// DS2-internal: the world keeps running under the game's own menus while linked (see ds2/world_pause.cpp).
namespace world_pause {

// Start-up: the detour on the game module's IsPaused.
void installEarly();

}  // namespace world_pause
