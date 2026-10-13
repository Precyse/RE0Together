#pragma once
#include <cstdint>

namespace coop_pools {

// Gives the two "coop_player" memory pools (cpu and gpu) a new block of current size * (maxPlayers - 1) bytes, once
// per process. Called just before net_set_mode runs its co-op case, which resets the pools. Does nothing for
// `maxPlayers` 2, and nothing (logged) when a pool does not look like the expected one.
void resize(uint32_t maxPlayers);

}  // namespace coop_pools
