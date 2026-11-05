#pragma once
// The puppet set of hooks (DD2 design players.md 3-4): binds a built body when the engine reports it ready, and keeps
// the game's own brain off our puppets at the decision layer while their motion update runs: navigation destinations,
// action requests, locomotion and idle requests and local HP writes are refused on a puppet unless they come from our
// driver; the game's own cleanup of stray and lost pawns is refused for our ids. One counter per layer. The same gates
// hold the guest's enemy mirrors (enemy_mirror.h), with the monster overrides in the mirror set, and hand every HP
// change of a mirror to it.
#include <cstdint>

namespace puppet_brain {

// Installs the set all or nothing; false when a row is refused (puppets are then never driven).
bool install();
bool installed();

// Installs the mirror set (after install); false when a row is refused (no enemy sync then).
bool installMirror();

// Game thread, every 30 Hz step: a summary line every 30 s when a gate refused something.
void tick(uint64_t nowMs);

}  // namespace puppet_brain
