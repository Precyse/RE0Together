#pragma once
// The hired support pawns of a co-op world (DD2 design players.md 7). They run unsynced on every machine, and a party holds
// two sub pawns at most (PawnID SubPawn01 and SubPawn02), the seats a peer's main pawn needs. A guest dismisses the
// support pawns of the host's world once at its first co-op load; a machine seating a peer's main pawn dismisses just enough
// of them for it. A player's own main pawn (PawnID MainPawn) and our puppets are never touched. Needs the puppet calls.
#include <cstdint>

namespace support_pawns {

// Game thread, every 30 Hz step while the session is active.
void tick(uint64_t nowMs);

// Game thread: with both sub pawn seats taken, dismisses one hired support pawn (none when a seat is free or both seats hold
// puppets or the like).
void makeRoom();

}  // namespace support_pawns
