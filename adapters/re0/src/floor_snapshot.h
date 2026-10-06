#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "floor_pending.h"

// FLOOR_SNAPSHOT (0x0115), host to a joining guest, reliable: the floor changes the host has made or heard of since
// its last save or load (puts that were not taken again, takes of items the save still holds). The guest replays them
// like peer events, puts only where the item is not already there. Pure codec, no game, unit tested.
namespace floor_snapshot {

// More than the per-room cap over a handful of rooms; the rest is dropped.
constexpr size_t kMaxEvents = 256;

struct Header {
    uint16_t count;
    uint16_t reserved;
};
static_assert(sizeof(Header) == 4);

struct Entry {
    uint16_t room;
    uint8_t isTake;
    uint8_t reserved;
    uint32_t itemId;
    uint32_t count;
    floor_pending::Vec3 pos;
    floor_pending::Vec3 rot;
};
static_assert(sizeof(Entry) == 36);

inline std::vector<uint8_t> encode(const std::vector<floor_pending::RoomEvent>& events) {
    const size_t count = events.size() < kMaxEvents ? events.size() : kMaxEvents;
    std::vector<uint8_t> out(sizeof(Header) + count * sizeof(Entry));
    const Header header{static_cast<uint16_t>(count), 0};
    std::memcpy(out.data(), &header, sizeof(header));
    for (size_t i = 0; i < count; ++i) {
        const floor_pending::Event& event = events[i].event;
        const Entry entry{events[i].room, event.isTake, 0, event.itemId, event.count, event.pos, event.rot};
        std::memcpy(out.data() + sizeof(Header) + i * sizeof(Entry), &entry, sizeof(entry));
    }
    return out;
}

// The events, each a put marked onlyIfAbsent; nullopt when the size does not match the count.
inline std::optional<std::vector<floor_pending::RoomEvent>> decode(std::span<const uint8_t> bytes) {
    Header header;
    if (bytes.size() < sizeof(header)) return std::nullopt;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.count > kMaxEvents || bytes.size() != sizeof(Header) + header.count * sizeof(Entry)) return std::nullopt;
    std::vector<floor_pending::RoomEvent> out;
    for (size_t i = 0; i < header.count; ++i) {
        Entry entry;
        std::memcpy(&entry, bytes.data() + sizeof(Header) + i * sizeof(Entry), sizeof(entry));
        out.push_back({entry.room, {entry.isTake != 0, entry.itemId, entry.count, entry.pos, entry.rot, true}});
    }
    return out;
}

}  // namespace floor_snapshot
