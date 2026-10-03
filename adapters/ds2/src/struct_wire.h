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

constexpr uint8_t kKindLadder = 10;
constexpr uint8_t kLadderTailBytes = 0x18;  // the ladder's own fields after the base descriptor
constexpr size_t kMaxTailBytes = 0x40;
constexpr size_t kTransformBytes = 0x40;  // WorldTransform: position (3 doubles), orientation (3x3 floats), padding

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

// Only the kinds whose own fields are known are carried.
inline bool supported(const Create& c) { return c.kind == kKindLadder && c.tailBytes == kLadderTailBytes; }

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
