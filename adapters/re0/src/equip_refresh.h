#pragma once
#include "character_owner.h"

// Re-equips a remote-owned character the way the game's menu close does (0x5d8040): the weapon resource sets are
// released and requested first, and the equip step (weapon type, weapon unit swap, aim) runs once every request has
// loaded. A received inventory block whose equipped slot changed needs it, since no menu closed here. Every weapon
// change of either character is logged.
namespace equip_refresh {

// Game thread: a received inventory block changed the character's equipped slot.
void request(character_owner::Character character);

// Game thread, every tick for each character: advances a pending re-equip and logs a weapon change.
void tick(character_owner::Character character);

}  // namespace equip_refresh
