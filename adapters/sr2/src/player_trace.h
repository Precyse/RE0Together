#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 traces of the players area (trace=1 only, read-only; docs/SR2_NOTES.md, "Player traces"):
//   TF1  every bullet spawn (0x9C1B90): who fired, which machine owns the human, how long since its last shot,
//   TF2  every update of a player bundle (the vtable slot 0xE2B444 -> 0x8633D0): when a remote player's weapon state changes,
//   TL2  how many coop_player buffers the game builds at start (0x9E9EB0).
// They tell whether player 3's guns fire too fast because of its own timing, the history it is replayed from, or a wrong owner.
namespace player_trace {

// The code behind TF1, TF2 and TL2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace player_trace
