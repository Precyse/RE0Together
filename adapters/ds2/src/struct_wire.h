#pragma once
// STRUCT_CREATE / STRUCT_REMOVE: structures the host's player places or removes (ladders so far), mirrored into the
// guest's world with the game's own player-build recipe (docs/DS2_NOTES.md, "Structure sync"). Both machines load the
// same save, so the construction id the host's game assigned names the structure on both.
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace struct_wire {

constexpr uint16_t kMsgStructCreate = proto::kFirstGameType + 15;  // 0x010F, host to all, reliable
constexpr uint16_t kMsgStructRemove = proto::kFirstGameType + 16;  // 0x0110, host to all, reliable
// A guest asks the host to build what its player placed: the same payload as STRUCT_CREATE with id kAssignId, so the
// host's own counter names the structure (the id counters of the two worlds start equal and would collide).
constexpr uint16_t kMsgStructRequest = proto::kFirstGameType + 42;  // 0x012A, guest to host, reliable
constexpr uint32_t kAssignId = 0xFFFFFFFF;                          // the game's own "assign me an id" (Init takes -1)

constexpr uint8_t kKindLadder = 10;
constexpr size_t kBaseDescriptorBytes = 0x328;  // the common part of every creation descriptor; the kind's own fields follow
constexpr size_t kMaxTailBytes = 0x40;
constexpr size_t kTransformBytes = 0x40;  // WorldTransform: position (3 doubles), orientation (3x3 floats), padding

// A player-buildable structure kind (the descriptor's category byte, +0x10): how many bytes of its own fields follow
// the common part, and the qwords among them that are left out because they may hold pointers or handles once the
// structure is in use (docs/DS2_NOTES.md, "Structure sync"). Kinds whose fields are not known are not carried.
struct KindInfo {
    uint8_t kind;
    uint8_t tailBytes;
    uint8_t skipFirst;  // offset (from the start of the tail) of a qword not to copy, or kNoSkip
    uint8_t skipSecond;
};
constexpr uint8_t kNoSkip = 0xFF;
constexpr KindInfo kKinds[] = {
    {2, 0x00, kNoSkip, kNoSkip},   // SafetyHouse
    {3, 0x00, kNoSkip, kNoSkip},   // Post
    {4, 0x08, kNoSkip, kNoSkip},   // WatchTower
    {5, 0x18, kNoSkip, kNoSkip},   // Catapult
    {6, 0x00, kNoSkip, kNoSkip},   // Charger
    {7, 0x00, kNoSkip, kNoSkip},   // RainShelter
    {9, 0x08, kNoSkip, kNoSkip},   // Zipline
    {10, 0x18, kNoSkip, kNoSkip},  // Ladder
    {11, 0x08, kNoSkip, kNoSkip},  // FieldRope (the climbing anchor): its length; the rope state is rebuilt by the game
    {12, 0x08, kNoSkip, kNoSkip},  // Bridge
    {14, 0x28, kNoSkip, kNoSkip},  // CatapultShell
    {15, 0x08, kNoSkip, kNoSkip},  // ChiralBridge
    {16, 0x08, kNoSkip, kNoSkip},  // JumpStand
    {20, 0x00, kNoSkip, kNoSkip},  // FastTravelStation
    {21, 0x18, 0x10, kNoSkip},     // ElectricWall (+0x338 may hold a pointer)
    {22, 0x28, 0x08, 0x1C},        // Shield (replaceable block: +0x330 and +0x344)
    {23, 0x30, 0x08, 0x1C},        // FixedGun
    {26, 0x10, kNoSkip, kNoSkip},  // BoringMachine
    {32, 0x28, 0x08, 0x1C},        // WaterGun
    {33, 0x28, 0x08, 0x1C},        // ChiralShockCannon
    {34, 0x10, kNoSkip, kNoSkip},  // CompactLight
};

inline const KindInfo* kindInfo(uint8_t kind) {
    for (const KindInfo& info : kKinds) {
        if (info.kind == kind) return &info;
    }
    return nullptr;
}

struct Create {
    uint8_t kind;
    uint8_t subKind;
    uint8_t level;
    uint8_t tailBytes;
    uint32_t id;  // the construction id (descriptor +0x6C)
    uint8_t guid[16];
    uint8_t transform[kTransformBytes];
    float durability;
};
static_assert(sizeof(Create) == 0x5C);

struct Remove {
    uint32_t id;
    uint8_t factor;
    uint8_t reserved[3];
};
static_assert(sizeof(Remove) == 8);

// A structure as the host's game placed it: the fixed fields and the kind's own tail.
struct Placed {
    Create create;
    std::vector<uint8_t> tail;
};

// Only the kinds whose own fields are known are carried, with their tail of the known length.
inline bool supported(const Create& c) {
    const KindInfo* info = kindInfo(c.kind);
    return info && c.tailBytes == info->tailBytes;
}

inline std::vector<uint8_t> encode(const Placed& placed) {
    std::vector<uint8_t> payload(sizeof(Create) + placed.tail.size());
    std::memcpy(payload.data(), &placed.create, sizeof(Create));
    if (!placed.tail.empty()) std::memcpy(payload.data() + sizeof(Create), placed.tail.data(), placed.tail.size());
    return payload;
}

// False for a payload of the wrong size, an unsupported kind or a tail of the wrong length (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, Placed& out) {
    Create create;
    if (payload.size() < sizeof(create)) return false;
    std::memcpy(&create, payload.data(), sizeof(create));
    if (!supported(create) || payload.size() != sizeof(create) + create.tailBytes) return false;
    out.create = create;
    out.tail.assign(payload.begin() + sizeof(create), payload.end());
    return true;
}

inline bool decode(std::span<const uint8_t> payload, Remove& out) {
    if (payload.size() != sizeof(Remove)) return false;
    std::memcpy(&out, payload.data(), sizeof(out));
    return true;
}

}  // namespace struct_wire
