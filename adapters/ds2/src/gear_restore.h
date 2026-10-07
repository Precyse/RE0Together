#pragma once
// The guest keeps its own gear across sessions: while this machine is a guest it writes what it gained since the join
// (backpack cargo and worn pieces) to <game>\coop\personal_gear.txt, and at the next join gives those pieces back through
// the game's own add calls (game::addCargo, game::addSlotPiece) unless Sam already holds them. Off unless adapter.ini
// gear_restore=1. The format and the difference logic are in gear_snapshot.h; levels are not covered.
#include "net_client.h"

namespace gear_restore {

void setEnabled(bool enabled);

// Start-up: registers the simulation-thread callback.
void installEarly();

// Net thread, every tick: whether this machine is a guest.
void tick(const SessionSnapshot& session);

}  // namespace gear_restore
