#pragma once
// FACT_SET: the host's changed story, order and progress facts, sent to the guests so their worlds follow the host's.
// The layout follows the game's own NetMsgSetFact* messages (docs/DS2_NOTES.md, "Fact sharing"): a fact UUID, a typed
// value, and the writer's two persistence flags; the message the engine would serialize is a NetMessage header plus
// two GUIDs, then a bool or a 32-bit value.
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace fact_wire {

constexpr uint16_t kMsgFactSet = proto::kFirstGameType + 11;  // 0x010B, host to all, reliable: Header, then Entries
constexpr uint32_t kMaxEntries = 256;                         // per message
constexpr size_t kUuidSize = 16;

constexpr uint8_t kKindBool = 1;  // value: 0 or 1
constexpr uint8_t kKindInt = 2;   // value: the int32 bits

// Entry::flags: the writer's 5th and 6th arguments (the fact context's persistence switches).
constexpr uint8_t kFlagArg5 = 1;
constexpr uint8_t kFlagArg6 = 2;

struct Header {
    uint32_t count;
    uint32_t reserved;
};
static_assert(sizeof(Header) == 8);

struct Entry {
    uint8_t kind;
    uint8_t flags;
    uint16_t reserved;
    uint8_t uuid[kUuidSize];
    uint32_t value;
};
static_assert(sizeof(Entry) == 24);

inline bool validKind(uint8_t kind) { return kind == kKindBool || kind == kKindInt; }

inline std::vector<uint8_t> encode(std::span<const Entry> entries) {
    const Header header{static_cast<uint32_t>(entries.size()), 0};
    std::vector<uint8_t> payload(sizeof(header) + entries.size_bytes());
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!entries.empty()) std::memcpy(payload.data() + sizeof(header), entries.data(), entries.size_bytes());
    return payload;
}

// False for a payload that is short, long, over the limit, or holds an entry of an unknown kind (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, std::vector<Entry>& out) {
    out.clear();
    Header header;
    if (payload.size() < sizeof(header)) return false;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.count > kMaxEntries || payload.size() != sizeof(header) + header.count * sizeof(Entry)) return false;
    std::vector<Entry> entries(header.count);
    if (header.count) std::memcpy(entries.data(), payload.data() + sizeof(header), header.count * sizeof(Entry));
    for (const Entry& entry : entries) {
        if (!validKind(entry.kind)) return false;
    }
    out = std::move(entries);
    return true;
}

}  // namespace fact_wire
