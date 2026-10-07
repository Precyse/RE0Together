#pragma once
// Wire payloads of the enemy replication messages (game types, opaque to the launcher).
#include <cstdint>

#include "protocol.h"

namespace enemy_protocol {

constexpr uint16_t kMsgHitRequest = proto::kFirstGameType + 0x10;   // to the room's enemy owner, reliable
constexpr uint16_t kMsgHitApplied = proto::kFirstGameType + 0x11;   // the owner's damage outcome, to all, reliable
constexpr uint16_t kMsgEnemyState = proto::kFirstGameType + 0x12;   // owner to all, unreliable, 20 Hz
constexpr uint16_t kMsgEnemyDecision = proto::kFirstGameType + 0x1A;  // owner to all, reliable

constexpr uint8_t kNoAttacker = 0xFF;  // HIT_APPLIED for damage no player dealt (attackerCharacterId)

// Payload of HIT_REQUEST and HIT_APPLIED: a HitInfo without its attacker pointer, the hit point relative to the enemy,
// and (HIT_APPLIED only) what the owner's damage function started from and ended with, so every machine replays the
// same outcome.
struct HitPayload {
    uint8_t slot;
    uint8_t attackerCharacterId;  // 0 Billy, 1 Rebecca, kNoAttacker (HIT_APPLIED only: just the HP outcome)
    uint8_t flag;
    uint8_t room;  // the sender's loaded scene: pool slots are reused by the next room's enemies
    float offset[3];  // hit point minus the enemy's position: the head/body test compares heights relative to the enemy
    int32_t rangeTier;
    int32_t attackType;
    int32_t a;
    int32_t b;
    int32_t hpBefore;     // HIT_APPLIED: the owner's enemy HP just before the hit
    int32_t hpAfter;      // HIT_APPLIED: and just after it
    uint32_t random[4];   // HIT_APPLIED: the owner's random state just before the hit (game::RandomState)
};
static_assert(sizeof(HitPayload) == 56);

// ENEMY_DECISION: the base-family enemy's new record, chosen by the owner's think step, and the pose it chose from.
struct Decision {
    uint8_t slot;
    uint8_t room;
    uint16_t seq;  // per sender, increasing (wraps)
    int32_t action[4];
    float pos[3];
    float quat[4];
};
static_assert(sizeof(Decision) == 48);

// ENEMY_STATE: header, then `count` entries.
struct StateHeader {
    uint8_t count;
    uint8_t room;  // the sender's loaded scene, like a hit's room
    uint16_t seq;  // per sender, increasing (wraps): a late snapshot is dropped
};
static_assert(sizeof(StateHeader) == 4);
constexpr uint8_t kNoTarget = 0xFF;
struct EnemyEntry {
    uint8_t slot;
    uint8_t target;  // character id (character_owner::Character) the enemy chases, or kNoTarget
    uint8_t reserved[2];
    uint32_t vtable;
    int32_t hp;
    float pos[3];
    float quat[4];
    int32_t action[4];  // the enemy's behaviour record {state, id, a, b}
};
static_assert(sizeof(EnemyEntry) == 56);

}  // namespace enemy_protocol
