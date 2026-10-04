#pragma once
// ENEMY_SPAWN / ENEMY_STATE / ENEMY_GONE: the host's enemies, mirrored as puppets on the guests (docs/DS2_NOTES.md,
// "Enemy puppets"). The host owns every enemy; it announces each one, reports its pose at 10 Hz and says when it died
// or went away. A guest builds the puppet from its own vetoed spawn request with the same entity UUID.
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace enemy_wire {

constexpr uint16_t kMsgEnemySpawn = proto::kFirstGameType + 0x1B;  // 0x011B, host to all, reliable: EnemySpawn
constexpr uint16_t kMsgEnemyState = proto::kFirstGameType + 0x1C;  // 0x011C, host to all, unreliable: u16 count + EnemyState[]
constexpr uint16_t kMsgEnemyGone = proto::kFirstGameType + 0x1D;   // 0x011D, host to all, reliable: EnemyGone
constexpr size_t kUuidSize = 16;
constexpr size_t kMaxStatesPerMessage = 16;

struct Pose {
    double position[3];
    float rotation[9];  // three rows (right, forward, up)
    float reserved;
};
static_assert(sizeof(Pose) == 64);

struct EnemySpawn {
    uint16_t netId;  // host-assigned, from 1
    uint16_t reserved;
    uint8_t entityUuid[kUuidSize];    // the entity's UUID: equal to the guest's parked request for spawnpoint spawns
    uint8_t resourceUuid[kUuidSize];  // the entity resource's UUID
    uint32_t reserved2;
    Pose pose;
};
static_assert(sizeof(EnemySpawn) == 104);

constexpr uint8_t kStateDead = 1;
constexpr uint8_t kHealthUnknown = 255;

struct EnemyState {
    uint16_t netId;
    uint8_t healthRatio;  // 0-254, kHealthUnknown when not reported
    uint8_t flags;        // kStateDead
    uint32_t reserved;
    Pose pose;
    float velocity[3];
    uint32_t reserved2;
};
static_assert(sizeof(EnemyState) == 88);

enum class GoneReason : uint8_t { Despawned = 0, Died = 1 };

struct EnemyGone {
    uint16_t netId;
    uint8_t reason;  // GoneReason
    uint8_t reserved;
};
static_assert(sizeof(EnemyGone) == 4);

template <class T>
bool decodeOne(std::span<const uint8_t> payload, T& out) {
    if (payload.size() != sizeof(T)) return false;
    std::memcpy(&out, payload.data(), sizeof(T));
    return true;
}

inline std::vector<uint8_t> encodeStates(std::span<const EnemyState> states) {
    const uint16_t count = static_cast<uint16_t>(states.size());
    std::vector<uint8_t> out(sizeof(count) + states.size() * sizeof(EnemyState));
    std::memcpy(out.data(), &count, sizeof(count));
    if (count) std::memcpy(out.data() + sizeof(count), states.data(), states.size() * sizeof(EnemyState));
    return out;
}

// False for a payload whose size does not match its count or that carries more than one message may.
inline bool decodeStates(std::span<const uint8_t> payload, std::vector<EnemyState>& out) {
    uint16_t count = 0;
    if (payload.size() < sizeof(count)) return false;
    std::memcpy(&count, payload.data(), sizeof(count));
    if (count > kMaxStatesPerMessage || payload.size() != sizeof(count) + count * sizeof(EnemyState)) return false;
    out.resize(count);
    if (count) std::memcpy(out.data(), payload.data() + sizeof(count), count * sizeof(EnemyState));
    return true;
}

}  // namespace enemy_wire
