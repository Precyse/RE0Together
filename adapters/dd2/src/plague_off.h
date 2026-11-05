#pragma once
// Dragonsplague off on every machine while coop=1 (DD2 design progression.md 10): an outbreak kills NPCs in the host's
// world and would spread through a pawn whose owner plays on another machine. A pawn never gains possession points or a
// higher possession level, the inn's outbreak check answers "none" and the outbreak's NPC kill does nothing. Each refusal
// is counted; a 60 s line reports new ones.
#include <cstdint>

namespace plague_off {

// Installs the plague set; false (logged) when a row is missing or moved.
bool install();

// Game thread, from the tick: the refusals line.
void tick(uint64_t nowMs);

}  // namespace plague_off
