#pragma once
// ENEMY_HIT / PLAYER_HIT: combat between a guest and the host's enemies (docs/DS2_NOTES.md, "Enemy
// combat"). The host owns every enemy, so a guest's damage to a puppet is sent to the host instead of being applied, a
// host enemy's damage to the partner's body is sent to the guest to apply on its own player (an enemy's death is
// ENEMY_GONE(Died), enemy_wire.h). A hit carries the plain fields of the engine's DamageParams; the receiver builds the engine's own
// parameters from them. An enemy is named by its net id (the host's ENEMY_SPAWN) and its entity UUID (the same on both
// machines), so a hit still finds its enemy when a table is behind.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

#include "enemy_wire.h"
#include "protocol.h"

namespace combat_wire {

constexpr uint16_t kMsgEnemyHit = proto::kFirstGameType + 0x20;    // 0x0120, guest to host, reliable: EnemyHit
constexpr uint16_t kMsgPlayerHit = proto::kFirstGameType + 0x21;   // 0x0121, host to one guest, reliable: PlayerHit

constexpr float kMaxHitAmount = 10000.0f;   // damage of one hit above this is refused (the strongest hits are in the hundreds)
constexpr float kMaxVectorComponent = 1.0e6f;
constexpr int32_t kNoPart = -1;
constexpr int32_t kMaxPartIndex = 255;
constexpr uint16_t kNoEnemy = 0;  // net ids start at 1

struct EnemyRef {
    uint16_t netId;  // 0 = no enemy (an attacker that is not a tracked enemy)
    uint16_t reserved;
    uint8_t uuid[enemy_wire::kUuidSize];  // all zero = no enemy
};
static_assert(sizeof(EnemyRef) == 20);

// The plain fields of DamageParams (0xB8 bytes in the engine).
struct HitFields {
    float amount;       // +0x78
    uint32_t flags;     // +0x08, EDamageFlags
    int32_t partIndex;  // +0x68, kNoPart when the hit is on no part
    uint32_t reserved;
    float origin[4];    // +0x00, 16-byte vector
    float impulse[4];   // +0x10, the hit's direction and force (MsgDamage Impulse)
    float normal[4];    // +0x50, the surface normal (MsgDamage Normal)
};
static_assert(sizeof(HitFields) == 64);

struct EnemyHit {
    EnemyRef enemy;  // the victim
    HitFields hit;   // the attacker is the sending player
};
static_assert(sizeof(EnemyHit) == 84);

struct PlayerHit {
    EnemyRef attacker;  // the enemy that hit the partner's body, none for damage with no enemy behind it
    HitFields hit;
};
static_assert(sizeof(PlayerHit) == 84);

inline bool isNone(const EnemyRef& ref) {
    for (const uint8_t byte : ref.uuid) {
        if (byte) return false;
    }
    return true;
}

inline bool finite(const float (&vector)[4]) {
    for (const float component : vector) {
        if (!std::isfinite(component) || std::abs(component) > kMaxVectorComponent) return false;
    }
    return true;
}

// An enemy that is named: a net id and a UUID.
inline bool validEnemy(const EnemyRef& ref) { return ref.netId != kNoEnemy && !isNone(ref); }

// A hit with a sane amount and part and finite vectors.
inline bool validHit(const HitFields& hit) {
    return std::isfinite(hit.amount) && hit.amount > 0.0f && hit.amount <= kMaxHitAmount &&
           hit.partIndex >= kNoPart && hit.partIndex <= kMaxPartIndex && finite(hit.origin) && finite(hit.impulse) &&
           finite(hit.normal);
}

template <class T>
bool decodeOne(std::span<const uint8_t> payload, T& out) {
    if (payload.size() != sizeof(T)) return false;
    std::memcpy(&out, payload.data(), sizeof(T));
    return true;
}

// False for a payload of the wrong size, an unnamed enemy or an insane hit.
inline bool decode(std::span<const uint8_t> payload, EnemyHit& out) {
    return decodeOne(payload, out) && validEnemy(out.enemy) && validHit(out.hit);
}

// The attacker may be unnamed (none, or an enemy the host does not track).
inline bool decode(std::span<const uint8_t> payload, PlayerHit& out) {
    return decodeOne(payload, out) && validHit(out.hit);
}

}  // namespace combat_wire
