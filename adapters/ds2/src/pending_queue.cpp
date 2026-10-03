#include "pending_queue.h"

#include <algorithm>
#include <iterator>

#include "reject_counters.h"

bool PendingQueue::hold(uint64_t objectId, uint16_t type, uint8_t slot, std::span<const uint8_t> payload, TimeUs now) {
    if (held_.size() >= kMaxHeld) {
        reject_counters::count(type, reject_counters::Reason::Overflow);
        return false;
    }
    held_.push_back({objectId, type, slot, std::vector<uint8_t>(payload.begin(), payload.end()), now});
    return true;
}

std::vector<PendingQueue::Message> PendingQueue::take(uint64_t objectId) {
    std::vector<Message> out;
    const auto firstOther = std::stable_partition(held_.begin(), held_.end(),
                                                  [objectId](const Message& m) { return m.objectId != objectId; });
    out.assign(std::make_move_iterator(firstOther), std::make_move_iterator(held_.end()));
    held_.erase(firstOther, held_.end());
    return out;
}

size_t PendingQueue::expire(TimeUs now) {
    size_t dropped = 0;
    for (const Message& message : held_) {
        if (now - message.heldAt < kHoldUs) continue;
        reject_counters::count(message.type, reject_counters::Reason::Expired);
        ++dropped;
    }
    std::erase_if(held_, [now](const Message& m) { return now - m.heldAt >= kHoldUs; });
    return dropped;
}
