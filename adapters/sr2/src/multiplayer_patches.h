#pragma once

// The exe edits of milestone 9 (docs/SR2_NOTES.md): the game reacts to players 3 and 4 the way it reacts to the one partner
// of 2-player co-op. The tables are session_sites.h, world_sites.h, mission_sites.h and trace_sites.h; each is one phase,
// byte-checked and written all or nothing, and only with 3 or more players and the milestone 4 partner logic.
namespace multiplayer_patches {

// Checks the bytes of every site and logs each difference. True when all match.
bool sitesMatch();

// Writes the trace sites of the cutscene end, Lua errors and fades one by one, with trace=1 at any max_players (inside a hooks
// batch); a site whose bytes differ is left out and logged.
void installScriptTraces();

// Writes the phases (the trace phase only with `trace`, the guest leave phase only when the leave path edits of milestone
// 5i are in); a phase whose sites cannot all be written is left out and logged. The sites that wait for a live probe (the
// mission placement rows) are written with `experimental` only. True when every phase went in.
bool install(bool trace, bool leavePathInstalled, bool experimental);


}  // namespace multiplayer_patches
