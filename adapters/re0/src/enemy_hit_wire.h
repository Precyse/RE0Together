#pragma once
// HIT_REQUEST / HIT_APPLIED payloads to and from the damage function's arguments. Pure; tested in
// tests/enemy_hit_wire_test.cpp.
#include <cstdint>

#include "enemy_protocol.h"
#include "game.h"

namespace enemy_hit_wire {

// The hit point travels relative to the enemy: the damage function only gets its address, and its head/body test
// compares the point's height with the enemy's, so each machine rebuilds it around its own enemy.
inline enemy_protocol::HitPayload encode(uint8_t slot, uint8_t attackerCharacterId, uint8_t room, uint32_t vtable,
                                         const game::HitPoint& point, const float (&enemyPos)[3],
                                         const game::HitInfo& info) {
    return {slot,
            attackerCharacterId,
            info.flag,
            room,
            {point.x - enemyPos[0], point.y - enemyPos[1], point.z - enemyPos[2]},
            info.rangeTier,
            info.attackType,
            info.a,
            info.b,
            0,
            0,
            {},
            vtable,
            0,
            {},
            {}};
}

// The owner's HP and random state just before its damage function ran (the HP after it is set once it has run).
inline void stamp(enemy_protocol::HitPayload& hit, int32_t hpBefore, const game::RandomState& random) {
    hit.hpBefore = hpBefore;
    for (size_t i = 0; i < random.size(); ++i) hit.random[i] = random[i];
}

inline game::HitPoint pointOf(const enemy_protocol::HitPayload& hit, const float (&enemyPos)[3]) {
    return {enemyPos[0] + hit.offset[0], enemyPos[1] + hit.offset[1], enemyPos[2] + hit.offset[2]};
}

inline game::RandomState randomOf(const enemy_protocol::HitPayload& hit) {
    return {hit.random[0], hit.random[1], hit.random[2], hit.random[3]};
}

// The HitInfo the receiver passes, with its own object for the attacking character.
inline game::HitInfo infoOf(const enemy_protocol::HitPayload& hit, void* attacker) {
    return {hit.rangeTier, hit.attackType, hit.a, hit.b, attacker, hit.flag, {}};
}

}  // namespace enemy_hit_wire
