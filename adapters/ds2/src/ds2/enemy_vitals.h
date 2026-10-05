#pragma once
// DS2-internal: an enemy's health, read on the host and written on the guest's puppet (see ds2/enemy_vitals.cpp).
#include <cstdint>

namespace enemy_vitals {

// Health as 0-254 of the maximum (enemy_wire::kHealthUnknown when the entity cannot say). The entity must exist.
uint8_t readHealth(uintptr_t entity);

// Whether the enemy is dead: the engine's dead flag is set, or its life is 0 (live: the partner's hits take a MULE to 0 life and
// the flag never showed). The entity must exist.
bool isDead(uintptr_t entity);

// The address of the float holding an enemy's current life (its damage component's life block), 0 when it has none.
uintptr_t healthAddress(uintptr_t entity);

// Sets the puppet's life to `health` (never to zero: only the host's death report kills a puppet). The entity must exist.
void applyHealth(uintptr_t entity, uint8_t health);

}  // namespace enemy_vitals
