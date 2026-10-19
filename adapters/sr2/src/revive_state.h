#pragma once
#include <cstddef>
#include <cstdint>

#include "revive_rules.h"

// What the downed, revive and respawn wait rules read from the game (milestone 16 W2, docs/SR2_NOTES.md "Downed, revive and
// notoriety (M16 W2)"): the remote players as revive_rules::Player, the local player's own bleed-out and wait, the game clock, and
// the host's respawn wait of each remote slot.
namespace revive_state {

// With trace=1 the thunks of this area log their decisions.
void setTrace(bool trace);
bool trace();

// True once the downed and revive sites are in: a remote's wait is then the one this table holds. Without them it is the game's own
// query, which answers for the default partner only.
void setInstalled(bool installed);

// The game clock in milliseconds (wraps at sr2::kClockWrapMs).
int32_t now();

// The remote player humans in slot order, up to `capacity`; the number written.
size_t remotes(revive_rules::Player* out, size_t capacity);

// The game's own query (0x8CBDB0) for `human`: its respawn wait is pending. Only the local player and the default partner answer.
bool gameWaiting(uintptr_t human);

// The local player is bleeding out / waits to respawn, by the game's own state.
bool localDown();
bool localWaiting();

// The remote in `slot` died: it waits to respawn for sr2::kRespawnWaitMs from now.
void recordDeath(size_t slot);

// The game cleared its notoriety and both waits: so does the table.
void clearWaits();

}  // namespace revive_state
