#pragma once
// The host's time of day on every guest (cd design world.md 12). The host sends FIELD_TIME every 5 s while Ready; a
// guest whose own time is more than about two game minutes off posts SetFieldTimeReq to its own server (the host's
// hour on the guest's nearest day, field_time_rule.h), which saves it with the field and answers with SetFieldTimeAck.
// Fixed-time levels are never synced; nothing is posted while paused or outside Ready.
#include <cstdint>
#include <span>

#include "funnels.h"

namespace world_time {

// After the catalogue: SetFieldTimeReq to post, SetFieldTimeAck to confirm. False when a name is missing.
bool resolveOpcodes();

// Main tick.
void onEvent(const funnels::Event& event);
void tick(uint64_t nowMs);

// Link routing: FIELD_TIME from the host. False for any other type or a payload that does not decode.
bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload, uint64_t nowMs);

struct Stats {
    uint32_t posted = 0, confirmed = 0;
};
Stats stats();

}  // namespace world_time
