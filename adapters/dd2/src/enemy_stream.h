#pragma once
// The host half of the enemy sync (adapter.ini enemy_sync=1; DD2 design world.md 1-2). The host simulates every enemy:
// it streams the state of those near a guest (ENEMY_STATE, 10 Hz), replays the hits a guest's own bodies dealt to its
// mirrors through the engine's own direct-damage entry with the guest's puppet as the attacker (ENEMY_HIT), and answers
// a guest's missing keys with their generate records (ENEMY_WANT, ENEMY_SPAWN, 4 per second).
#include <cstdint>
#include <span>

namespace enemy_stream {

void setEnabled(bool on);

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

// Session routing, host only. False for a payload that does not decode.
bool onHit(uint8_t slot, std::span<const uint8_t> payload, uint64_t nowMs);
bool onWant(uint8_t slot, std::span<const uint8_t> payload);

}  // namespace enemy_stream
