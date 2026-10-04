#pragma once
// BT_ENV and CATCHER_EVENT: the host's BT (beached thing) world state, sent to the guests (docs/DS2_NOTES.md, "BT and
// catcher sync"). BT_ENV is the set of BT-active regions (the DSWeatherManager's per-region flags); the forecast that
// brings BT weather (types RainnyBt, RainyBtTar) already travels in WORLD_ENV, so this is its sibling, not a copy.
// CATCHER_EVENT is one catcher activation (a territory or its tar) as the game's own call made it on the host.
#include <cstdint>
#include <cstring>
#include <span>

#include "env_wire.h"
#include "protocol.h"

namespace bt_wire {

constexpr uint16_t kMsgBtEnv = proto::kFirstGameType + 0x28;         // 0x0128, host to all, reliable: BtEnv
constexpr uint16_t kMsgCatcherEvent = proto::kFirstGameType + 0x29;  // 0x0129, host to all, reliable: CatcherEvent
constexpr int kRegionCount = env_wire::kRegionCount;
constexpr size_t kUuidSize = 16;
constexpr uint8_t kFlagTerritoryBool = 1;  // the territory activation's bool argument (the locator's +0x185)

static_assert(kRegionCount == 64, "the region set is one bit per region in a u64");

#pragma pack(push, 1)
struct BtEnv {
    uint64_t activeRegions;  // bit r set = region r is BT-active
};
static_assert(sizeof(BtEnv) == 8);

enum class CatcherKind : uint8_t {
    Territory = 1,  // the catcher manager activates a territory locator (the catcher, its tar and the area presentation)
    Tar = 2,        // a locator's tar activates on its own (the tar areas around a catcher)
};

struct CatcherEvent {
    uint8_t kind;  // CatcherKind
    uint8_t flags;
    uint8_t reserved[2];
    uint8_t locator[kUuidSize];  // the territory locator's UUID (the same in every copy of the world)
};
static_assert(sizeof(CatcherEvent) == 20);
#pragma pack(pop)

// The BT-active set from the per-region flag bytes the game keeps (any non-zero byte is active).
inline uint64_t maskOf(std::span<const uint8_t, kRegionCount> flags) {
    uint64_t mask = 0;
    for (int region = 0; region < kRegionCount; ++region) {
        if (flags[region]) mask |= uint64_t{1} << region;
    }
    return mask;
}

inline bool isActive(uint64_t mask, int region) { return (mask >> region) & 1; }

// The regions whose state differs between what this world has and what the host reports.
inline uint64_t regionsToChange(uint64_t have, uint64_t wanted) { return have ^ wanted; }

// False for a payload of the wrong size (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, BtEnv& out) {
    if (payload.size() != sizeof(BtEnv)) return false;
    std::memcpy(&out, payload.data(), sizeof(out));
    return true;
}

// False for a payload of the wrong size, an unknown kind or flag bits that mean nothing (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, CatcherEvent& out) {
    if (payload.size() != sizeof(CatcherEvent)) return false;
    CatcherEvent event;
    std::memcpy(&event, payload.data(), sizeof(event));
    const auto kind = static_cast<CatcherKind>(event.kind);
    if (kind != CatcherKind::Territory && kind != CatcherKind::Tar) return false;
    if (event.flags & ~kFlagTerritoryBool) return false;
    out = event;
    return true;
}

}  // namespace bt_wire
