#pragma once
#include <cstdint>

// Runs the game's own equip step for one character, the one it runs for both characters when the inventory menu
// closes: the weapon type of the equipped slot, the weapon unit swap and the aim refresh. A remote-owned character
// whose equipped slot changed in a received inventory block needs it, since no menu closed here.
namespace equip_refresh {

// Game thread. False when the engine faulted.
bool run(uintptr_t player);

}  // namespace equip_refresh
