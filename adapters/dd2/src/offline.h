#pragma once
// Isolation from Capcom's online services during a session (DD2 design phase 3): once per session, as soon as a
// matching peer is linked, NetworkManager.requestForceOfflinePlay, so no pawn-server traffic or rental can reach a
// co-op world. Solo play outside a session is untouched.
#include <cstdint>

namespace offline {

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

}  // namespace offline
