#pragma once
// The payload of ANIM_STATE and ANIM_EVENT (one layout, two message types).
//   AnimHeader {u32 seq, u16 count, u16 flags}
//   u64 sentUs            only when flags has kFlagTimestamped: the sender's nowUs() when it sent the report
//   `count` entries {u16 index, u8 type, value}, the value 1 byte (bool), 4 bytes (int, float) or 16 bytes (quat) by
//                         the type (the engine's variable types 0, 1, 2, 3)
// A report without kFlagTimestamped (the layout before timestamps existed) decodes with no timestamp.
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "anim_change.h"
#include "protocol.h"
#include "time_us.h"

namespace anim_wire {

constexpr uint16_t kMsgAnimState = proto::kFirstGameType + 10;  // 0x010A, to all, unreliable
constexpr uint16_t kMsgAnimEvent = proto::kFirstGameType + 0x16;  // 0x0116, to all, reliable: a pulse the state may have missed

struct AnimHeader {
    uint32_t seq;
    uint16_t count;
    uint16_t flags;
};
static_assert(sizeof(AnimHeader) == 8);

constexpr uint16_t kFlagSnapshot = 1;       // the report holds every variable, not just the changed ones
constexpr uint16_t kFlagTimestamped = 2;    // a u64 sender time follows the header
constexpr size_t kEntryHeaderSize = 3;      // u16 index, u8 type

struct Report {
    AnimHeader header{};
    std::optional<TimeUs> sentUs;
    std::vector<remote_animation::Change> changes;
};

// Always timestamped.
inline std::vector<uint8_t> encode(uint32_t seq, bool snapshot, TimeUs sentUs,
                                   std::span<const remote_animation::Change> changes) {
    std::vector<uint8_t> out(sizeof(AnimHeader) + sizeof(uint64_t));
    for (const remote_animation::Change& change : changes) {
        const size_t size = remote_animation::valueBytes(change.type);
        out.resize(out.size() + kEntryHeaderSize + size);
        uint8_t* at = out.data() + out.size() - kEntryHeaderSize - size;
        std::memcpy(at, &change.index, sizeof(change.index));
        at[sizeof(change.index)] = change.type;
        std::memcpy(at + kEntryHeaderSize, change.value, size);
    }
    const AnimHeader header{seq, static_cast<uint16_t>(changes.size()),
                            static_cast<uint16_t>(kFlagTimestamped | (snapshot ? kFlagSnapshot : 0))};
    const uint64_t stamp = static_cast<uint64_t>(sentUs);
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + sizeof(header), &stamp, sizeof(stamp));
    return out;
}

// False (and nothing returned) for a payload that is short or holds an entry of a type that is not mirrored or one cut
// off. Bytes after the last entry are ignored.
inline bool decode(std::span<const uint8_t> payload, Report& out) {
    out = {};
    Report report;
    if (payload.size() < sizeof(AnimHeader)) return false;
    std::memcpy(&report.header, payload.data(), sizeof(AnimHeader));
    size_t at = sizeof(AnimHeader);
    if (report.header.flags & kFlagTimestamped) {
        uint64_t stamp;
        if (payload.size() < at + sizeof(stamp)) return false;
        std::memcpy(&stamp, payload.data() + at, sizeof(stamp));
        report.sentUs = static_cast<TimeUs>(stamp);
        at += sizeof(stamp);
    }
    for (uint16_t i = 0; i < report.header.count; ++i) {
        if (at + kEntryHeaderSize > payload.size()) return false;
        remote_animation::Change change{};
        std::memcpy(&change.index, payload.data() + at, sizeof(change.index));
        change.type = payload[at + sizeof(change.index)];
        const size_t size = remote_animation::valueBytes(change.type);
        if (size == 0 || at + kEntryHeaderSize + size > payload.size()) return false;
        std::memcpy(change.value, payload.data() + at + kEntryHeaderSize, size);
        report.changes.push_back(change);
        at += kEntryHeaderSize + size;
    }
    out = std::move(report);
    return true;
}

}  // namespace anim_wire
