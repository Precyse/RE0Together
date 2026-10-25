#pragma once
#include <vector>

#include "flag_diff.h"

// Host-ordered story flags (partner_mode=state): a guest's native flag changes go to the host as FLAG_PROPOSE 0x0175;
// the host applies proposals in arrival order and broadcasts every change as a numbered FLAG_COMMIT 0x0176; a guest
// applies commits in order and a committed value wins over its own. flag_sync owns the game access (the setter and
// the raw enemy bits); this module decides what is applied and in which order.
namespace flag_sequencer {

// Registers the two message handlers. False when a type could not be registered.
bool enable();

// Game thread: the changes the local flags made since the last check. The host commits them at once, a guest
// proposes them to the host.
void submitLocal(const std::vector<flag_diff::Change>& changes);

// Game thread, once per check: the changes flag_sync must apply now, in order. `words` are the live flags; the host
// numbers and broadcasts what it takes from proposals, a guest takes what the commits say (and asks for a snapshot on
// a gap in their numbers).
std::vector<flag_diff::Change> collect(const flag_diff::Words& words);

// Game thread, host: the serial of the last commit issued, 0 before the first (what JOIN_EXTRAS tells a joiner).
uint32_t issuedSerial();

// Game thread: the serial of the last commit this machine's flags hold (the host's issued one, a guest's last applied
// one), 0 before the first. What the world digest is stamped with.
uint32_t appliedSerial();

// Game thread: flag changes of this machine still in flight, 0 when the flags are settled: a host's proposals not yet
// ordered, a guest's proposals not yet answered.
uint32_t pendingCount();

// Game thread, guest: the join snapshot just applied held every commit up to `serial`; the next one in order is
// serial + 1 and older ones are duplicates. Call after `reset` (flag_sync::applySnapshot).
void adoptSerial(uint32_t serial);

// Game thread: forget the order and the unanswered proposals (the link dropped, or a snapshot replaced the flags).
void reset();

// Game thread: this machine loaded a player save. Like `reset`, and the proposals and commits that arrived for the world
// before the load and were not collected yet are dropped.
void onLocalLoad();

}  // namespace flag_sequencer
