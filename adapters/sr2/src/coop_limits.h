#pragma once
#include <cstdint>

namespace coop_limits {

// Hooks net_set_mode; once its co-op case has written 2 players the adapter writes `maxPlayers` over both limits.
bool install(uint32_t maxPlayers);

// True while net_set_mode is running on any thread.
bool insideNetSetMode();

}  // namespace coop_limits
