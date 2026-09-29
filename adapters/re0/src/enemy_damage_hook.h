#pragma once
#include <cstdint>

#include "game.h"

namespace enemy_damage_hook {

// Patches the damage slot of every enemy vtable. Call once after the game code is decrypted.
bool install();

// Runs the enemy's own damage function for a hit received from the peer (the hook lets it through).
bool applyNetworkHit(uintptr_t enemy, uintptr_t attacker, float distance, game::HitInfo& info);

}  // namespace enemy_damage_hook
