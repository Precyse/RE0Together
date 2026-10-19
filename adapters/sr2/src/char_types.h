#pragma once
#include <cstdint>

// M16 W1 (docs/SR2_NOTES.md, "One character type per remote"): every remote player is drawn with its own character type. Co-op gives
// all remotes styletest_pc_mp2, one record and one stream slot; the game's own MP characters styletest_pc_mp3 and up have their own
// types and live in stream category 3, which co-op never sizes.
//   CT1  sizes category 3 for `max_players - 2` characters in co-op (0x9D65D0 returns at once in co-op),
//   CT2  streams styletest_pc_mp3.. at a co-op level load (0x69C030, after the block for mp2), and
//   CT3  fills the char table with the type of each remote's pool index (0x9D7FD0) instead of mp2 for all.
// With the sites of the exe not as expected nothing is written and every remote keeps styletest_pc_mp2.
namespace char_types {

// Installs CT1 to CT3 as one set, only with 3 or more players. trace=1 logs the types streamed and filled.
void install(uint32_t maxPlayers, bool trace);

}  // namespace char_types
