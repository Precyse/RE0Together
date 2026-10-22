#pragma once
// PING_SET: the user markers (map and HUD pings) a player has placed, sent to the partner (docs/DS2_NOTES.md, "User markers").
// The game keeps them in a fixed pool of 100 DSUserMarker objects, so the message is the whole set: the receiver replaces what
// it held for that sender and an empty set clears it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace ping_wire {

constexpr uint16_t kMsgPingSet = proto::kFirstGameType + 0x3A;  // 0x013A, each player to all, reliable: PingSetHeader, Ping[count]
constexpr size_t kMaxPings = 100;                               // the pool: 5 groups of 20 points
constexpr uint8_t kMaxGroup = 4;
constexpr uint8_t kMaxIndex = 19;
constexpr float kMaxWorldMetres = 100000.0f;

#pragma pack(push, 1)
struct PingSetHeader {
    uint16_t count;
    uint16_t reserved;
};
static_assert(sizeof(PingSetHeader) == 4);

struct Ping {
    uint64_t id;          // the sender's marker id (DSUserMarker +0x20), unique per sender
    float position[3];    // world metres, Z up
    uint8_t group;        // 0..4
    uint8_t index;        // 0..19
    uint8_t sequence;     // 1 or more: a placed point, not a line's interpolated one
    uint8_t reserved;
};
static_assert(sizeof(Ping) == 24);
#pragma pack(pop)

inline bool plausible(const Ping& ping) {
    for (const float metres : ping.position) {
        if (!std::isfinite(metres) || std::fabs(metres) > kMaxWorldMetres) return false;
    }
    return ping.group <= kMaxGroup && ping.index <= kMaxIndex && ping.sequence != 0 && ping.reserved == 0;
}

// The set in a stable order (by id), so an unchanged set always encodes to the same bytes.
inline void sortById(std::vector<Ping>& pings) {
    std::sort(pings.begin(), pings.end(), [](const Ping& a, const Ping& b) { return a.id < b.id; });
}

// Equal sets in the same order (sortById both first).
inline bool sameSet(const std::vector<Ping>& a, const std::vector<Ping>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Ping)) == 0);
}

// The payload of a set of at most kMaxPings.
inline std::vector<uint8_t> encode(std::span<const Ping> pings) {
    const PingSetHeader header{static_cast<uint16_t>(pings.size()), 0};
    std::vector<uint8_t> out(sizeof(header) + pings.size_bytes());
    std::memcpy(out.data(), &header, sizeof(header));
    if (!pings.empty()) std::memcpy(out.data() + sizeof(header), pings.data(), pings.size_bytes());
    return out;
}

// False for a size that is not the header plus `count` pings, a count over kMaxPings, a non-zero header reserved field, a ping
// that is not plausible or two pings with one id (nothing is returned). An empty set is valid.
inline bool decode(std::span<const uint8_t> payload, std::vector<Ping>& out) {
    PingSetHeader header;
    if (payload.size() < sizeof(header)) return false;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.count > kMaxPings || header.reserved != 0) return false;
    if (payload.size() != sizeof(header) + header.count * sizeof(Ping)) return false;
    std::vector<Ping> pings(header.count);
    if (header.count) std::memcpy(pings.data(), payload.data() + sizeof(header), header.count * sizeof(Ping));
    if (!std::all_of(pings.begin(), pings.end(), plausible)) return false;
    sortById(pings);
    if (std::adjacent_find(pings.begin(), pings.end(), [](const Ping& a, const Ping& b) { return a.id == b.id; }) != pings.end()) {
        return false;
    }
    out = std::move(pings);
    return true;
}

}  // namespace ping_wire
