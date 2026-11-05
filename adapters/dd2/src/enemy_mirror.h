#pragma once
// The guest half of the enemy sync (adapter.ini enemy_sync=1; DD2 design world.md 1 "Generation on guests" and
// "Observer copies"). Against the host's roster: our enemies whose key the host has become mirrors (brain off through the
// puppet gates, driven from ENEMY_STATE with the puppet driver, HP set to the host's, killed natively when the host's
// dies); our extras are removed; the missing are asked for (ENEMY_WANT) and generated from the host's records. A hit our
// own Arisen or main pawn lands on a mirror changes no HP here and goes to the host (ENEMY_HIT).
#include <cstdint>
#include <span>

namespace enemy_mirror {

void setEnabled(bool on);

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

// Any thread (the gates): one of a mirror's character or controllers.
bool isMirrorObject(const void* object);

// Any thread, from the HP gate: a hit about to change a mirror's HP (`info` is its DamageInfo).
void onDamage(void* hitController, void* info);

// Session routing, from the host only. False for a payload that does not decode.
bool onState(std::span<const uint8_t> payload, uint64_t nowMs);
bool onSpawn(std::span<const uint8_t> payload);

}  // namespace enemy_mirror
