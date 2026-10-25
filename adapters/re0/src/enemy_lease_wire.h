#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

#include "contact_rule.h"
#include "enemy_policy.h"
#include "game.h"
#include "protocol.h"
#include "room_authority_wire.h"

// Wire format of the contact router and the grab lease (partner_mode=state, docs/RE0_NOTES.md "State mode: contact and
// lease"). All three are reliable and carry the room authority's epoch for the scene: a mismatch is stale. Pure, no
// game access, unit tested.
namespace enemy_lease_wire {

constexpr uint16_t kMsgEnemyLease = proto::kFirstGameType + 0x63;     // 0x0163, authority to the victim's owner
constexpr uint16_t kMsgEnemyLeaseEnd = proto::kFirstGameType + 0x64;  // 0x0164, holder to all (or the authority, on a lapse)
constexpr uint16_t kMsgPlayerHit = proto::kFirstGameType + 0x65;      // 0x0165, an enemy's simulator to the victim's owner

constexpr uint8_t kLastCharacter = 1;     // character_owner::Character::Rebecca
constexpr int32_t kMaxRecordWord = 0xfff;  // the state and action id of a behaviour record (sanity bound)

// ENEMY_LEASE: the enemy's simulation moves to the machine of the character it just grabbed. `record` is the enemy's
// behaviour record after its update (the granted action), `hp` its HP (the stream may only lower it), `pos` / `quat`
// its pose and `point` the hit point relative to the enemy. `senderMs` is the authority's clock: the holder starts the
// grant when its copy of the enemy visibly reaches the victim.
struct EnemyLease {
    uint32_t token;  // never 0 in the low 16 bits (they travel with every streamed pose)
    uint32_t roomEpoch;
    uint32_t senderMs;
    uint16_t scene;
    uint8_t slot;
    uint8_t classIndex;
    uint16_t spawnId;
    uint8_t victim;
    uint8_t kind;
    uint8_t level;
    uint8_t pad;  // zero
    int16_t hp;
    int32_t record[4];
    float pos[3];
    uint32_t quat;  // smallest three, pose_wire::packQuat
    float point[3];
    uint16_t spawnRecord;  // the enemy's spawn record: the receiver resolves its own pool slot from it (slots differ per machine)
    uint16_t reserved;     // zero
};
static_assert(sizeof(EnemyLease) == 72);
static_assert(std::is_trivially_copyable_v<EnemyLease>);

// ENEMY_LEASE_END: the lease is over; the authority resumes the enemy from this state at step 0. `reason` is a
// contact_rule::End.
struct EnemyLeaseEnd {
    uint32_t token;
    uint32_t roomEpoch;
    uint16_t scene;
    uint8_t slot;
    uint8_t reason;
    int16_t hp;
    uint16_t spawnRecord;  // as EnemyLease
    int32_t record[4];
    float pos[3];
    uint32_t quat;
};
static_assert(sizeof(EnemyLeaseEnd) == 48);
static_assert(std::is_trivially_copyable_v<EnemyLeaseEnd>);

// PLAYER_HIT: a plain hit (flinch, knock-back, knock-down, death) on a character the sender's enemy reached. The
// victim's owner calls the engine's own damage entry with its local copy of the enemy at `senderMs` plus the render
// delay. `seq` numbers the sender's hits.
struct PlayerHit {
    uint32_t roomEpoch;
    uint32_t senderMs;
    uint32_t seq;
    uint16_t scene;
    uint8_t slot;
    uint8_t classIndex;
    uint16_t spawnId;
    uint8_t victim;
    uint8_t kind;
    uint8_t level;
    uint8_t pad;           // zero
    uint16_t spawnRecord;  // as EnemyLease
    float point[3];        // relative to the enemy
};
static_assert(sizeof(PlayerHit) == 36);
static_assert(std::is_trivially_copyable_v<PlayerHit>);

inline bool finite(const float (&v)[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

inline bool recordFits(const int32_t (&record)[4]) {
    return record[0] >= 0 && record[0] <= kMaxRecordWord && record[1] >= 0 && record[1] <= kMaxRecordWord;
}

// Fields every message shares: the room, the enemy's slot and class, a victim that is a character.
inline bool contactFits(uint16_t scene, uint8_t slot, uint8_t classIndex, uint8_t victim, uint8_t kind) {
    return scene < room_authority_wire::kSceneIdLimit && slot < game::kEnemyPoolSlots && classIndex < enemy_policy::kClassCount &&
           victim <= kLastCharacter && kind < contact_rule::kReactionKindCount;
}

inline bool valid(const EnemyLease& lease) {
    return (lease.token & 0xffff) != 0 && contactFits(lease.scene, lease.slot, lease.classIndex, lease.victim, lease.kind) &&
           lease.pad == 0 && lease.reserved == 0 && lease.spawnRecord < game::kSpawnRecordCount && lease.hp > 0 &&
           recordFits(lease.record) && finite(lease.pos) && finite(lease.point);
}

inline bool valid(const EnemyLeaseEnd& end) {
    return end.scene < room_authority_wire::kSceneIdLimit && end.slot < game::kEnemyPoolSlots &&
           end.reason != static_cast<uint8_t>(contact_rule::End::None) && end.reason < contact_rule::kEndCount && end.spawnRecord < game::kSpawnRecordCount &&
           recordFits(end.record) && finite(end.pos);
}

inline bool valid(const PlayerHit& hit) {
    return contactFits(hit.scene, hit.slot, hit.classIndex, hit.victim, hit.kind) && hit.pad == 0 &&
           hit.spawnRecord < game::kSpawnRecordCount && finite(hit.point);
}

// The payload as a message of exactly the right size whose fields are in range, or nothing.
template <typename Message>
std::optional<Message> decode(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(Message)) return std::nullopt;
    Message message;
    std::memcpy(&message, payload.data(), sizeof(message));
    if (!valid(message)) return std::nullopt;
    return message;
}

}  // namespace enemy_lease_wire
