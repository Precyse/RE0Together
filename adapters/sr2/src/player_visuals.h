#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 14 (docs/SR2_NOTES.md, "Remote player visuals (M14)"): with 3 players only one remote was drawn.
//   PV1  the cutscene start (0x6BFB50, block 0x6C04FD) hid the first remote in list order, the newest, and
//   PV2  the cutscene end (0x6C44C0, block 0x6C561B) restored the default partner, so the newest remote stayed hidden. Both now
//        go over every remote player human. With one remote they are the shipped blocks.
//   PV3 to PV10  the skin composite pool (0x21F2988, 2 objects of two 512x512 textures: one for the local player, one for the
//        partner) holds `max_players` objects, so every char instance gets a skin.
namespace player_visuals {

// Moves the skin composite pool to a block of `maxPlayers` objects by rewriting the eight immediates that reach it, all or
// nothing, once at start-up (before the game builds the pool at 0x9EEFA0). Does nothing at 2 players or when the sites differ or the
// pool is already built; logs why. True when the pool was moved.
bool installSkinPool(uint32_t maxPlayers);

// trace=1 logs the humans hidden and restored by a cutscene.
void setTrace(bool trace);

// The code behind PV1 and PV2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace player_visuals
