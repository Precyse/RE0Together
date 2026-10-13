#pragma once
#include <cstdint>

namespace coop_pools {

// Gives the two "coop_player" memory pools (cpu and gpu) a new block of current size * (maxPlayers - 1) bytes and the
// "coop_allocators" pool (the deltacomp baselines) one sized for the 8-peer instances (coopAllocatorsPoolSize), once
// per process. Called just before net_set_mode runs its co-op case, which resets the pools. All or none: nothing
// changes (and the reason is logged) when a pool does not look like the expected one. True when the pools are resized,
// false for `maxPlayers` 2 or when they were left alone.
bool resize(uint32_t maxPlayers);

}  // namespace coop_pools
