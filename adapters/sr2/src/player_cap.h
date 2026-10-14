#pragma once
#include <cstdint>

// Milestone 5: a third and further players are accepted. The session info maximum (advertised in the server browser
// and checked by every join) becomes `max_players`, and isCoop stops requiring exactly 2 members. Two players behind one NAT are told apart by
// their real source address (home_network).
namespace player_cap {

// Checks all sites (the player cap and the home network edits), then writes them together: nothing when any differs, or
// when `maxPlayers` is 2. Run before any session is created: a session keeps the maximum it was created with. `trace`
// logs the home network decisions. True when the code was changed.
bool install(uint32_t maxPlayers, bool trace);

}  // namespace player_cap
