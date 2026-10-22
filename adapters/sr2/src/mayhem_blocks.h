#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The Mayhem per-player counters and score blocks for 3 or 4 players (milestone 17 wave 3, rows MB0 to MB4 of walk_sites.h; docs/SR2_NOTES.md,
// "Walks and the Mayhem counter (milestone 17 wave 3)"). The game keeps two destruction counters per district entry (the local player's and
// everyone else's) and two score blocks; the default partner keeps the game's second ones, every other remote has its own, in the adapter.
namespace mayhem_blocks {

// {nullptr, nullptr} for a thunk that belongs to another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace mayhem_blocks
