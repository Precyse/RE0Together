#pragma once
// DS2-internal: whether the engine still holds an entity, by UUID (EntityManagerGame's UUID map). The enemy modules keep
// raw entity pointers and check them with this before touching them.
#include <cstdint>

namespace ds2 {

// `uuid` is the entity's 16-byte UUID (entity +0x10). Any thread; reads the map without its lock.
bool entityExists(const uint8_t* uuid);

}  // namespace ds2
