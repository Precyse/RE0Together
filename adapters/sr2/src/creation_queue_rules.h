#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>

// The pure rules of the creation queue of a session member (unit tested): the objects whose creation is yet to be sent to
// that member. The game's own writers (0x805000 every networked create, 0x8051F0 the join batch, 0x804940) and the leave path
// (LV3) append to it; the game never checks its capacity, and the entry after the last one is the queue's own count.
namespace creation_queue_rules {

constexpr uint32_t kCapacity = 0x100;  // handles a member's queue holds
constexpr size_t kMaxOverflow = 0x4000;  // handles an overflow list holds (the object table has 4096 slots)

inline bool hasRoom(uint32_t count) { return count < kCapacity; }

// A record's created mask has a bit per member slot; queuing a creation for a member clears its bit.
inline uint16_t maskAfterQueue(uint16_t mask, uint8_t memberSlot) {
    return static_cast<uint16_t>(mask & ~(1u << memberSlot));
}

// The handles that did not fit a member's queue, in order, per member. The game sends and clears the queue every send tick;
// after that the oldest of these move into it (at most a full queue), and go out on the next tick. The list is bounded: past
// kMaxOverflow the oldest handle is dropped, as a queue that is never sent would be in an unchecked game.
class Overflow {
public:
    void add(uintptr_t member, uint32_t handle) {
        std::deque<uint32_t>& list = lists_[member];
        if (list.size() >= kMaxOverflow) list.pop_front();
        list.push_back(handle);
    }

    // Moves up to `room` of the member's oldest handles to `out`. The count moved.
    size_t take(uintptr_t member, uint32_t* out, size_t room) {
        const auto found = lists_.find(member);
        if (found == lists_.end()) return 0;
        std::deque<uint32_t>& list = found->second;
        const size_t count = std::min(room, list.size());
        std::copy_n(list.begin(), count, out);
        list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(count));
        if (list.empty()) lists_.erase(found);
        return count;
    }

    size_t pending(uintptr_t member) const {
        const auto found = lists_.find(member);
        return found == lists_.end() ? 0 : found->second.size();
    }

private:
    std::map<uintptr_t, std::deque<uint32_t>> lists_;
};

}  // namespace creation_queue_rules
