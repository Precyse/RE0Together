#pragma once
// Wire payloads of the enemy replication messages (game types, opaque to the launcher).
#include <cstdint>

#include "protocol.h"

namespace enemy_protocol {

constexpr uint16_t kMsgHitRequest = proto::kFirstGameType + 0x10;   // to the room's enemy owner, reliable
constexpr uint16_t kMsgHitApplied = proto::kFirstGameType + 0x11;   // the owner's applied hit, to all, reliable
constexpr uint16_t kMsgEnemyState = proto::kFirstGameType + 0x12;   // owner to all, unreliable, 20 Hz

// Payload of HIT_REQUEST and HIT_APPLIED: a HitInfo without its attacker pointer, the hit point relative to the enemy,
// and (HIT_APPLIED only) what the owner's damage function started from, so every machine replays the same outcome.
struct HitPayload {
    uint8_t slot;
    uint8_t attackerCharacterId;  // 0 Billy, 1 Rebecca
    uint8_t flag;
    uint8_t room;  // the sender's loaded scene: pool slots are reused by the next room's enemies
    float offset[3];  // hit point minus the enemy's position: the head/body test compares heights relative to the enemy
    int32_t rangeTier;
    int32_t attackType;
    int32_t a;
    int32_t b;
    int32_t hpBefore;     // HIT_APPLIED: the owner's enemy HP just before the hit
    uint32_t random[4];   // HIT_APPLIED: the owner's random state just before the hit (game::RandomState)
};
static_assert(sizeof(HitPayload) == 52);

// ENEMY_STATE: a count byte and the sender's loaded scene (a byte, like a hit's room), then `count` entries.
constexpr size_t kStateHeaderSize = 2;
constexpr size_t kStateCountByte = 0;
constexpr size_t kStateRoomByte = 1;
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
