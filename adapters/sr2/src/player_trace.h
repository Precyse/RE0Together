#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 traces of the players area (trace=1 only, read-only; docs/SR2_NOTES.md, "Player traces"):
//   TF1  every shot (0x9C1B90 runs per human per frame; a shot re-arms the weapon's next shot time): who fired, which machine owns the human, how long since its last shot,
//   TF2  every update of a player bundle (the vtable slot 0xE2B444 -> 0x8633D0): when a remote player's weapon state changes,
//   TL2  how many coop_player buffers the game builds at start (0x9E9EB0),
//   BUT  a player bundle packet (the unpack 0x8669D0, vtable slot 0xE2B454) from a sender that is not the owner of the object.
// They tell whether player 3's guns fire too fast because of its own timing, the history it is replayed from, or a wrong owner.
namespace player_trace {

// The code behind TF1, TF2, TL2 and BUT; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace player_trace
