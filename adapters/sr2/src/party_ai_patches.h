#pragma once

// The exe edits of milestone 16 wave 2 (docs/SR2_NOTES.md, "Homies, followers and NPC AI (milestone 16 wave 2)"): the followers a
// leader hands to a remote, the mode enters and the NPC sight, range and team tests act for every remote, not for the one default
// partner. The tables are party_ai_sites.h; each is one phase, byte-checked and written all or nothing, and only with 3 or more
// players and the milestone 4 partner logic.
namespace party_ai_patches {

// Writes the phases (the trace phase only with `trace`), inside a hooks batch; a phase that differs or cannot be written is left
// out and logged, and the next one still goes in.
void install(bool trace);

}  // namespace party_ai_patches
