#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "enemy_policy.h"
#include "game.h"
#include "pose_wire.h"
#include "protocol.h"
#include "room_authority_wire.h"

// Wire format of the enemy observers (partner_mode=state, docs/RE0_NOTES.md "State mode: enemy observers"): the
// simulator's enemies sampled at 20 Hz (ENEMY_POSE, unreliable) and the motion requests their engine made
// (ENEMY_MOTION, reliable), both in batches of at most kMaxEntries entries behind one 16-byte header. Pure, no game
// access; clocks and numbering follow pose_wire (the sender's `pose_wire::clockMs`).
namespace enemy_observer_wire {

constexpr uint16_t kMsgEnemyPose = proto::kFirstGameType + 0x60;    // 0x0160, simulator to all, unreliable
constexpr uint16_t kMsgEnemyMotion = proto::kFirstGameType + 0x61;  // 0x0161, simulator to all, reliable

constexpr size_t kMaxEntries = 18;  // 16 + 18 * 60 = 1096 bytes, under the unreliable MTU
constexpr uint8_t kNoTarget = 0xff;
constexpr uint8_t kLastTargetCharacter = 1;  // character_owner::Character::Rebecca

constexpr uint8_t kFlagAlive = 0x1;
constexpr uint8_t kFlagTerminal = 0x2;  // the enemy died: sent once more, reliably
constexpr uint8_t kFlagRemoved = 0x4;   // the unit left the pool: sent once, reliably
constexpr uint8_t kFlagLeased = 0x8;    // sent by a lease holder (enemy_lease): the simulation of this enemy moved to the victim's machine
constexpr uint8_t kKnownFlags = kFlagAlive | kFlagTerminal | kFlagRemoved | kFlagLeased;

// The first bytes of every batch.
struct Header {
    uint32_t senderMs;   // the simulator's clock when the batch was made
    uint32_t seq;        // per sender, one number per batch of either type
    uint32_t roomEpoch;  // room_authority's epoch for `scene`: a mismatch is stale and dropped
    uint16_t scene;      // the simulator's loaded scene: pool slots are reused per room
    uint8_t count;
    uint8_t pad;
};
static_assert(sizeof(Header) == 16);
static_assert(std::is_trivially_copyable_v<Header>);

// One enemy of ENEMY_POSE.
struct PoseEntry {
    uint8_t slot;        // the sender's sEnemy pool slot (hits name it; the receiver binds by spawnRecord)
    uint8_t classIndex;  // index into enemy_policy::kClasses
    uint8_t flags;
    uint8_t target;      // character the enemy chases, or kNoTarget
    uint16_t spawnId;    // low word of the spawn record's id (+0x10)
    uint16_t spawnRecord;  // index of the spawn record the enemy was created from
    int16_t hp;          // clamped to 16 bits; a dead enemy holds -1
    uint16_t hitAttr;    // +0x16ec & 0x3ff
    float pos[3];
    uint32_t quat;       // smallest three, pose_wire::packQuat
    uint16_t motion0;
    uint16_t motion1;
    float frame0;
    uint16_t frame1;     // fixed point, 1/64 frame
    uint16_t leaseToken; // low 16 bits of the lease token (never 0) while kFlagLeased is set, else 0
    int32_t record[4];   // +0x67a4: {state, action id, a, b}
    uint32_t flags6a0c;  // read by the player's aim filter and weapon hit path
};
static_assert(sizeof(PoseEntry) == 60);
static_assert(std::is_trivially_copyable_v<PoseEntry>);

// One motion request of ENEMY_MOTION: the arguments of a cAnimControl::set call on an enemy.
struct MotionEntry {
    uint8_t slot;
    uint8_t layer;
    uint16_t motionNo;
    uint32_t flags;
    float interp;
    float start;
    uint16_t dtMs;     // how long before the header's senderMs the engine made the request
    uint16_t seqLow;   // low 16 bits of the sender's running request number
};
static_assert(sizeof(MotionEntry) == 20);
static_assert(std::is_trivially_copyable_v<MotionEntry>);

template <typename Entry>
struct Batch {
    Header header;
    std::vector<Entry> entries;
};
using PoseBatch = Batch<PoseEntry>;
using MotionBatch = Batch<MotionEntry>;

inline bool finite(float value) { return std::isfinite(value); }

inline int16_t clampHp(int32_t hp) { return static_cast<int16_t>(std::clamp<int32_t>(hp, INT16_MIN, INT16_MAX)); }

// Payloads for `entries`, split into batches of at most kMaxEntries. `header` carries the clock, epoch and scene; each
// batch takes the next number from `seq`.
template <typename Entry>
std::vector<std::vector<uint8_t>> encode(Header header, std::span<const Entry> entries, uint32_t& seq) {
    std::vector<std::vector<uint8_t>> payloads;
    for (size_t first = 0; first < entries.size(); first += kMaxEntries) {
        const size_t count = std::min(kMaxEntries, entries.size() - first);
        header.seq = ++seq;
        header.count = static_cast<uint8_t>(count);
        header.pad = 0;
        std::vector<uint8_t>& payload = payloads.emplace_back(sizeof(Header) + count * sizeof(Entry));
        std::memcpy(payload.data(), &header, sizeof(Header));
        std::memcpy(payload.data() + sizeof(Header), entries.data() + first, count * sizeof(Entry));
    }
    return payloads;
}

// The header of a payload of the right size for its own count, or nothing: 1..kMaxEntries entries, a scene below the
// engine's limit, the pad zero.
template <typename Entry>
std::optional<Header> decodeHeader(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(Header)) return std::nullopt;
    Header header;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.count == 0 || header.count > kMaxEntries || header.pad != 0 ||
        payload.size() != sizeof(Header) + header.count * sizeof(Entry) ||
        header.scene >= room_authority_wire::kSceneIdLimit) {
        return std::nullopt;
    }
    return header;
}

// A leased entry names its lease; every other entry carries no token.
inline bool leaseTokenFits(const PoseEntry& entry) { return ((entry.flags & kFlagLeased) != 0) == (entry.leaseToken != 0); }

inline bool valid(const PoseEntry& entry) {
    const bool targetOk = entry.target <= kLastTargetCharacter || entry.target == kNoTarget;
    return entry.slot < game::kEnemyPoolSlots && entry.classIndex < enemy_policy::kClassCount &&
           entry.spawnRecord < game::kSpawnRecordCount && (entry.flags & ~kKnownFlags) == 0 && targetOk && finite(entry.pos[0]) && finite(entry.pos[1]) &&
           finite(entry.pos[2]) && finite(entry.frame0) && leaseTokenFits(entry);
}

inline bool valid(const MotionEntry& entry) {
    return entry.slot < game::kEnemyPoolSlots && entry.layer < pose_wire::kMotionLayers && finite(entry.interp) &&
           finite(entry.start);
}

// A received batch of the right size whose every entry is in range, or nothing.
template <typename Entry>
std::optional<Batch<Entry>> decode(std::span<const uint8_t> payload) {
    const std::optional<Header> header = decodeHeader<Entry>(payload);
    if (!header) return std::nullopt;
    Batch<Entry> batch{*header, std::vector<Entry>(header->count)};
    std::memcpy(batch.entries.data(), payload.data() + sizeof(Header), header->count * sizeof(Entry));
    if (!std::all_of(batch.entries.begin(), batch.entries.end(), [](const Entry& entry) { return valid(entry); })) {
        return std::nullopt;
    }
    return batch;
}

}  // namespace enemy_observer_wire
