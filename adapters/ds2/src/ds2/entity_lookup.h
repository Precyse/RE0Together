#pragma once
// DS2-internal: whether the engine still holds an entity, by UUID (EntityManagerGame's UUID map). The enemy modules keep
// raw entity pointers and check them with this before touching them; the combat code holds only UUIDs and resolves them
// here.
#include <array>
#include <cstdint>

namespace ds2 {

// `uuid` is the entity's 16-byte UUID (entity +0x10). Any thread; reads the map without its lock.
bool entityExists(const uint8_t* uuid);

// The entity with this UUID (EntityManager::GetEntityByUUID), 0 when the engine has none. Any thread.
uintptr_t entityByUuid(const uint8_t* uuid);

// The entity's UUID (entity +0x10); false when the memory cannot be read.
bool entityUuid(uintptr_t entity, std::array<uint8_t, 16>& out);

// Whether the entity's dead flag (+0x98 bit 8) is set; an unreadable entity counts as dead.
bool entityIsDead(uintptr_t entity);

// The engine's EntityManagerGame (the object of the damage function), 0 before it exists.
uintptr_t entityManager();

}  // namespace ds2
