#pragma once
#include <cstdint>
#include <optional>

// What the host knows beyond JOIN_SNAPSHOT, told to a joining or resyncing guest in partner_mode=state
// (JOIN_EXTRAS 0x0156, session_wire.h): the snapshot's serial, the host's room claims, the push block commits, the items
// taken since the last save or load and the serial of the host's last flag commit. The host sends it right after the
// snapshot, from the same game tick, so all of it describes one instant. The guest applies it once its snapshot is applied.
namespace join_extras {

// Registers JOIN_EXTRAS with the frame router (HostOnly). False when it could not be registered.
bool enable();

// Game thread, host: builds the extras from the room authority table, the prop journal, the item ledger and the flag
// sequencer and sends them with the next snapshot serial.
void send();

// The serial of the newest extras this host sent, 0 before the first. Any thread.
uint32_t lastSerial();

// Guest, any thread: a new JOIN_SNAPSHOT arrived, the game was loaded again or the link dropped; extras held for an older
// snapshot no longer belong to anything.
void reset();

// Game thread, guest: the snapshot (flags and inventories) has been applied; its extras may follow.
void onSnapshotApplied();

// Game thread, guest, every tick: applies the extras of the applied snapshot once they have arrived and returns their
// snapshot serial.
std::optional<uint32_t> applyArrived();

}  // namespace join_extras
