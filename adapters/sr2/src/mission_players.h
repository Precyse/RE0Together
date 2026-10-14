#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 MC3 to MC5 (docs/SR2_NOTES.md, "Mission players"): where the missions place or compare "the partner" with
// three or four players. All three are cosmetic and wait for the live probes T (overlapping players) and CP (a checkpoint
// with a passenger), so they are written with partner_experimental only.
//   MC3  the end position of a cutscene goes to each remote a step apart (cutscn_pkt sub 5),
//   MC4  a guest's checkpoint marks a car as shared when the lowest guest in it is the one that keeps it,
//   MC5  a teleport that a script sends to a guest moves it a step apart from the others.
namespace mission_players {

// The code behind MC3 to MC5; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace mission_players
