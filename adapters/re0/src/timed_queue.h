#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "stream_clock.h"

// Numbered items from a peer, handed out once, in sequence order, when the peer's time for them has come (pure, unit
// tested). `Item` has `senderMs` (the peer's clock) and `seq` (its sequence). A duplicate or an already handed-out
// sequence is dropped; an item far behind the time to show is dropped and counted instead of replayed.
namespace timed_queue {

constexpr size_t kMaxItems = 64;
constexpr int32_t kStaleMs = 1000;  // an item this far behind the shown time is not handed out

template <typename Item>
class TimedQueue {
public:
    void clear() {
        items_.clear();
        hasHandedOut_ = false;
    }

    void push(const Item& item) {
        if (hasHandedOut_ && !stream_clock::after(item.seq, lastHandedOut_)) return;
        for (const Item& known : items_) {
            if (known.seq == item.seq) return;
        }
        const auto at = std::find_if(items_.begin(), items_.end(), [&](const Item& known) { return stream_clock::after(known.seq, item.seq); });
        items_.insert(at, item);
        if (items_.size() > kMaxItems) items_.erase(items_.begin());
    }

    // Appends the items whose time is at or before `target` to `due`; returns how many were due but too old.
    uint32_t takeDue(uint32_t target, std::vector<Item>& due) {
        uint32_t stale = 0;
        size_t taken = 0;
        while (taken < items_.size() && !stream_clock::after(items_[taken].senderMs, target)) {
            const Item& item = items_[taken++];
            hasHandedOut_ = true;
            lastHandedOut_ = item.seq;
            if (static_cast<int32_t>(target - item.senderMs) > kStaleMs) {
                ++stale;
            } else {
                due.push_back(item);
            }
        }
        items_.erase(items_.begin(), items_.begin() + static_cast<std::ptrdiff_t>(taken));
        return stale;
    }

    size_t size() const { return items_.size(); }

private:
    std::vector<Item> items_;  // by sequence
    bool hasHandedOut_ = false;
    uint32_t lastHandedOut_ = 0;
};

}  // namespace timed_queue
