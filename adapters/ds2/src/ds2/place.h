#pragma once
// DS2-internal: moving an entity the way the game's scripts do (Entity::SetWorldTransform: entity lock, copy, dirty
// flag), then giving its mover the velocity it moves with so its own animation or physics can follow. Shared by the
// borrowed bodies and the partner's vehicles. Simulation thread only.
#include <cstdint>

#include "decima/world_transform.h"
#include "world_to_screen.h"

namespace ds2 {

// False when Entity::SetWorldTransform is not found or the call faulted.
bool placeEntity(uintptr_t entity, const decima::WorldTransform& transform, const world_to_screen::Vec3& velocity);

// Entity::PlaceOnWorldTransform: a teleport, which also resets a player mover's capsule (a plain SetWorldTransform on
// a player entity is written back by the mover). False when the call faulted.
bool teleportEntity(uintptr_t entity, const decima::WorldTransform& transform);

// Entity::SetVisible: the entity flag and the visibility change message to its components. False when the call faulted.
bool setEntityVisible(uintptr_t entity, bool visible);

// An entity's current transform, or false when unreadable.
bool entityTransform(uintptr_t entity, decima::WorldTransform& out);

}  // namespace ds2
