#pragma once
// Wire payloads of the enemy replication messages (game types, opaque to the launcher).
#include <cstdint>

#include "protocol.h"

namespace enemy_protocol {

constexpr uint16_t kMsgHitRequest = proto::kFirstGameType + 0x10;   // guest to host, reliable
constexpr uint16_t kMsgHitApplied = proto::kFirstGameType + 0x11;   // host to all, reliable
constexpr uint16_t kMsgEnemyState = proto::kFirstGameType + 0x12;   // host to all, unreliable, 20 Hz

// Payload of HIT_REQUEST and HIT_APPLIED: a HitInfo without its attacker pointer.
struct HitPayload {
    uint8_t slot;
    uint8_t attackerCharacterId;  // 0 Billy, 1 Rebecca
    uint8_t flag;
    uint8_t reserved;
    float distance;
    int32_t rangeTier;
    int32_t attackType;
    int32_t a;
    int32_t b;
};
static_assert(sizeof(HitPayload) == 24);

// ENEMY_STATE: one count byte, then `count` entries.
constexpr size_t kStateHeaderSize = 1;
constexpr uint8_t kNoTarget = 0xFF;
struct EnemyEntry {
    uint8_t slot;
    uint8_t target;  // character id (character_owner::Character) the enemy chases, or kNoTarget
    uint8_t reserved[2];
    uint32_t vtable;
    int32_t hp;
    float pos[3];
    float quat[4];
    int32_t action[4];  // the enemy's behaviour record {state, id, a, b} (enemy_action_rule.h)
};
static_assert(sizeof(EnemyEntry) == 56);

}  // namespace enemy_protocol
