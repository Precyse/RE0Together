#pragma once
// HIT_REQUEST / HIT_APPLIED payloads to and from the damage function's arguments. Pure; tested in
// tests/enemy_hit_wire_test.cpp.
#include <cstdint>

#include "enemy_protocol.h"
#include "game.h"

namespace enemy_hit_wire {

// The hit point travels by value: the damage function only gets its address, which means nothing on the peer.
inline enemy_protocol::HitPayload encode(uint8_t slot, uint8_t attackerCharacterId, uint8_t room,
                                         const game::HitPoint& point, const game::HitInfo& info) {
    return {slot, attackerCharacterId, info.flag, room, {point.x, point.y, point.z},
            info.rangeTier, info.attackType, info.a, info.b};
}

inline game::HitPoint pointOf(const enemy_protocol::HitPayload& hit) {
    return {hit.point[0], hit.point[1], hit.point[2]};
}

// The HitInfo the receiver passes, with its own object for the attacking character.
inline game::HitInfo infoOf(const enemy_protocol::HitPayload& hit, void* attacker) {
    return {hit.rangeTier, hit.attackType, hit.a, hit.b, attacker, hit.flag, {}};
}

}  // namespace enemy_hit_wire
