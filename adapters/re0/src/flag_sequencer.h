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

// Game thread: forget the order and the unanswered proposals (the link dropped, or a snapshot replaced the flags).
void reset();

}  // namespace flag_sequencer
