#pragma once
// Equipment of the players (cd design players.md 4). Sender: a hook on the in-process server's OtherEquipItemAck
// writer (every equip path ends there) and our server's UnequipItemAck keep the own Loadout since the current template;
// it goes out as CHAR_EQUIP on every change and to a peer that joins. Receiver: each peer's newest CHAR_EQUIP is
// replayed on its live copy of the same template generation (equip_rule::plan), again after every respawn.
#include <cstdint>
#include <span>

#include "funnels.h"

namespace equipment {

// After the catalogue: watches UnequipItemAck and resolves the two Acks it injects. False when a name is missing.
bool resolveOpcodes();

// Inside the hooks batch; false when the writer's bytes differ (equipment then stays as the template had it).
bool install();

// Main tick.
void onEvent(const funnels::Event& event);
void tick(uint64_t nowMs, bool peerJoined, std::span<const uint8_t> slotsPresent);

// Link routing: CHAR_EQUIP from a matching peer. False for any other type or a payload that does not decode.
bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload);

struct Stats {
    uint32_t sent = 0, applied = 0, writerDropped = 0;
};
Stats stats();

}  // namespace equipment
