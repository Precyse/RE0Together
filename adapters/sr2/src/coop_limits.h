#pragma once
#include <cstdint>

namespace coop_limits {

// Hooks net_set_mode and retargets its allocator-set activation call (limit_thunk), so the player limits are
// `maxPlayers` from the moment the co-op sets activate. Before its co-op case runs, the co-op pools are resized
// (coop_pools), the vehicle pool is moved to 64 when it is wanted (vehicle_pool) and, when the pools worked, replication is
// switched to 8 peers (replication_switch). After the original, any other mode than co-op puts the vehicle limits back.
bool install(uint32_t maxPlayers);

// True while net_set_mode is running on any thread.
bool insideNetSetMode();

}  // namespace coop_limits
