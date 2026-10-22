#pragma once

// The guest's free-roam autosave (docs/SR2_NOTES.md, "Stalled peers and rejoins"; rules in coop_autosave_rules.h): run from the
// session tick (session_tick.cpp), a guest that played coop_autosave_rules::kIntervalMs since its last save asks its game for
// the game's own autosave (0x696600) at a safe moment of free roam, so a game that dies loses minutes, not a session.
namespace coop_autosave {

// On the game thread, at each session tick.
void tick();

}  // namespace coop_autosave
