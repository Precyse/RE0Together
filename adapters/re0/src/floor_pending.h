#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Per-room queue of floor item events that could not be applied yet because the room is not loaded (or not settled).
// Not thread safe: game thread only. No game dependencies.
namespace floor_pending {

using Vec3 = std::array<float, 3>;

// A peer's drop position and ours differ by float noise and animation, so a pickup matches the nearest item of the
// same id within this distance.
constexpr float kMatchDistance = 50.0f;
constexpr size_t kMaxPendingPerRoom = 32;

// A put (isTake false) uses itemId, count, pos and rot; a take uses itemId and pos. A put with onlyIfAbsent is
// skipped when the room already holds that item there (a replayed journal must not duplicate what the save has).
struct Event {
    bool isTake;
    uint32_t itemId;
    uint32_t count;
    Vec3 pos;
    Vec3 rot;
    bool onlyIfAbsent = false;
};

struct RoomEvent {
    uint16_t room;
    Event event;
};

struct AddResult {
    bool cancelledPut;  // a take removed a matching pending put; nothing was stored
    bool droppedOldest;  // the room was full and its oldest event was discarded
};

class Queue {
public:
    // A take that matches a pending put of the same room cancels both; otherwise the event is appended, dropping the
    // room's oldest event when it already holds kMaxPendingPerRoom.
    AddResult add(uint16_t room, const Event& event);

    // The room's events in arrival order; the room is left empty.
    std::vector<Event> take(uint16_t room);

    size_t total() const;

    // Every stored event with its room, left in place (rooms in no particular order, each room's events in arrival order).
    std::vector<RoomEvent> all() const;

    void clear();

private:
    std::unordered_map<uint16_t, std::vector<Event>> m_rooms;
};

}  // namespace floor_pending
