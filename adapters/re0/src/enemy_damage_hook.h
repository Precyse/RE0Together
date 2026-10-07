#pragma once
#include <cstdint>

#include "game.h"

namespace enemy_damage_hook {

// Patches the damage slot of every enemy vtable. Call once after the game code is decrypted.
bool install();

// Runs the enemy's own damage function past the hook (an owner's hit or a replayed one). False for an unknown class.
bool runDamage(uintptr_t enemy, uintptr_t attacker, game::HitPoint& point, game::HitInfo& info);

}  // namespace enemy_damage_hook
