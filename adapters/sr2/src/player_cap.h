#pragma once
#include <cstdint>

// Milestone 5: a third and further players are accepted. The session info maximum (advertised in the server browser
// and checked by every join) becomes `max_players`, and isCoop stops requiring exactly 2 members.
namespace player_cap {

// Checks both sites, then writes them together (nothing when either differs, or when `maxPlayers` is 2). Run before
// any session is created: a session keeps the maximum it was created with. True when the code was changed.
bool install(uint32_t maxPlayers);

}  // namespace player_cap
