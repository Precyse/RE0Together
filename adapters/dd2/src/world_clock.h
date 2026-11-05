#pragma once
// The shared time of day: the host sends CLOCK every 5 s and when the hour changes; a guest sets its own clock through
// TimeManager.setTime when clock_rule says so. setTime's three Int32 parameters are matched by their names (day, hour,
// minute) at start; names that do not match leave the guest side off, logged with the names found.
#include <cstdint>
#include <span>

namespace world_clock {

// After calls::resolve.
void install();

// Game thread, every 30 Hz step while the session is active.
void tick(uint64_t nowMs);

// Guest, from the host only. False for a payload that does not decode.
bool onClock(std::span<const uint8_t> payload, uint64_t nowMs);

}  // namespace world_clock
