#pragma once
// Messages about an object this machine does not have yet (a vehicle or enemy that has not loaded, a body not spawned):
// held until the object appears, then handed back in arrival order. A message still waiting after `kHoldUs` is dropped
// and counted (reject_counters, Reason::Expired), so a peer talking about something that never loads cannot make the
// queue grow or be silently lost. Net thread only; time is passed in so it can be tested.
#include <cstdint>
#include <span>
#include <vector>

#include "time_us.h"

class PendingQueue {
public:
    static constexpr TimeUs kHoldUs = 10'000'000;
    static constexpr size_t kMaxHeld = 512;

    struct Message {
        uint64_t objectId;
        uint16_t type;
        uint8_t slot;  // the sender
        std::vector<uint8_t> payload;
        TimeUs heldAt;
    };

    // False (counted as Reason::Overflow) when the queue is full.
    bool hold(uint64_t objectId, uint16_t type, uint8_t slot, std::span<const uint8_t> payload, TimeUs now);

    // The object appeared: its held messages, oldest first, removed from the queue.
    std::vector<Message> take(uint64_t objectId);

    // Drops what has waited `kHoldUs`; returns how many.
    size_t expire(TimeUs now);

    size_t size() const { return held_.size(); }
    void clear() { held_.clear(); }

private:
    std::vector<Message> held_;
};
