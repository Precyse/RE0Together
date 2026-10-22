#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 1 (docs/SR2_NOTES.md, "Kick and session admin (milestone 16 wave 1)"): the kick menu of the host.
//   KA1  coop_kick_player (0x7EF5E0) opens a picker of the remote members when there are two or more, and chains the game's own
//        confirm for the chosen one;
//   KA2 to KA4  the three getter calls of the confirm, its callback and kick_coop_player answer the chosen remote;
//   KA5  the session end on a sign-in loss (0x69A1D6) removes every remote member, not only the partner.
// With one remote member every path is the shipped one.
namespace session_kick {

// The code behind a site; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

// Writes the two sets of session_kick_sites.h, each all or nothing (inside a hooks batch); logs which went in. `trace` adds the
// trace lines of the choice and the removal.
void install(bool trace);

}  // namespace session_kick
