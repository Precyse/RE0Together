#pragma once
// CRATER_SET: the host's set of voidout craters, sent to the guests (docs/DS2_NOTES.md, "BT and catcher sync"). A crater is
// a record of the DSEnvironmentManager, keyed by the UUID of the catcher's crater locator, which both copies of the world
// share; it is not a fact, so FACT_SET does not carry it. The message is the whole set: a guest adds what it lacks and
// removes what the host no longer has.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace crater_wire {

constexpr uint16_t kMsgCraterSet = proto::kFirstGameType + 0x37;  // 0x0137, host to all, reliable: CraterSetHeader, Crater[count]
constexpr size_t kUuidSize = 16;
constexpr size_t kMaxCraters = 64;
constexpr float kMaxRadius = 2000.0f;  // metres
constexpr uint8_t kMaxKind = 15;       // the locator's ShortCycle kind is a 4-bit field in the entry's flags

#pragma pack(push, 1)
struct CraterSetHeader {
    uint16_t count;
    uint16_t reserved;
};
static_assert(sizeof(CraterSetHeader) == 4);

struct Crater {
    uint8_t uuid[kUuidSize];  // the crater locator's ObjectUUID
    double position[3];
    float radius;
    uint16_t area;  // the area the crater belongs to (it is shown only while the player is in it)
    uint8_t kind;
    uint8_t reserved;
};
static_assert(sizeof(Crater) == 48);
#pragma pack(pop)

inline bool sameSet(const std::vector<Crater>& a, const std::vector<Crater>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Crater)) == 0);
}

// The set in a stable order (by UUID), so an unchanged set always encodes to the same bytes.
inline void sortByUuid(std::vector<Crater>& craters) {
    std::sort(craters.begin(), craters.end(),
              [](const Crater& a, const Crater& b) { return std::memcmp(a.uuid, b.uuid, kUuidSize) < 0; });
}

// The payload of a set of at most kMaxCraters.
inline std::vector<uint8_t> encode(std::span<const Crater> craters) {
    const CraterSetHeader header{static_cast<uint16_t>(craters.size()), 0};
    std::vector<uint8_t> out(sizeof(header) + craters.size_bytes());
    std::memcpy(out.data(), &header, sizeof(header));
    if (!craters.empty()) std::memcpy(out.data() + sizeof(header), craters.data(), craters.size_bytes());
    return out;
}

inline bool plausible(const Crater& crater) {
    return std::isfinite(crater.position[0]) && std::isfinite(crater.position[1]) && std::isfinite(crater.position[2]) &&
           crater.radius > 0 && crater.radius <= kMaxRadius && crater.kind <= kMaxKind && crater.reserved == 0;
}

// False for a size that is not the header plus `count` craters, a count over kMaxCraters, a non-zero reserved field or a
// crater that is not plausible (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, std::vector<Crater>& out) {
    CraterSetHeader header;
    if (payload.size() < sizeof(header)) return false;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.count > kMaxCraters || header.reserved != 0) return false;
    if (payload.size() != sizeof(header) + header.count * sizeof(Crater)) return false;
    std::vector<Crater> craters(header.count);
    if (header.count) std::memcpy(craters.data(), payload.data() + sizeof(header), header.count * sizeof(Crater));
    for (const Crater& crater : craters) {
        if (!plausible(crater)) return false;
    }
    out = std::move(craters);
    return true;
}

}  // namespace crater_wire
