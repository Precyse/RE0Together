#pragma once
// Which enemies carry which net id on this machine: net id <-> entity UUID. The host's enemy module fills it as it
// announces an enemy, a guest's combat module fills it from the host's ENEMY_SPAWN. Entities are never held, only their
// UUIDs, so an enemy the engine freed cannot be touched through it. Any thread.
#include <array>
#include <cstdint>
#include <vector>

#include "enemy_wire.h"

namespace enemy_directory {

using Uuid = std::array<uint8_t, enemy_wire::kUuidSize>;

struct Entry {
    uint16_t netId;
    Uuid uuid;
};

void set(uint16_t netId, const Uuid& uuid);
void forget(uint16_t netId);
void clear();

// 0 when the UUID is not a tracked enemy.
uint16_t netIdOf(const Uuid& uuid);
std::vector<Entry> all();

}  // namespace enemy_directory
