#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

// The pure rules of the remote vehicle trace (milestone 12 VT1, unit tested): why a remote vehicle's create failed, and how
// many attempts a net id needed. The create takes a creation data record first and then a vehicle from the static pool; the
// game retries a failed create until its window runs out (docs/SR2_NOTES.md, "Remote vehicles").
namespace vehicle_trace_rules {

constexpr size_t kMaxTrackedIds = 256;  // the game's object_create_data records

enum class Failure { CreationData, Pool, Other };

// The create asks for the creation data first, so a missing record is the reason before an empty pool; anything else is a
// category slot or the streaming. The arguments are the two free list heads (0 = empty).
inline Failure classify(uint32_t creationDataHead, uint32_t poolHead) {
    if (creationDataHead == 0) return Failure::CreationData;
    return poolHead == 0 ? Failure::Pool : Failure::Other;
}

inline const char* name(Failure failure) {
    switch (failure) {
        case Failure::CreationData: return "creation data";
        case Failure::Pool: return "pool";
        default: return "other";
    }
}

// What the census line reports about the creates seen.
struct Totals {
    uint32_t creates = 0;
    uint32_t failed = 0;
    uint32_t pool = 0;
    uint32_t creationData = 0;
    uint32_t other = 0;

    void record(bool created, Failure failure) {
        ++creates;
        if (created) return;
        ++failed;
        switch (failure) {
            case Failure::CreationData: ++creationData; break;
            case Failure::Pool: ++pool; break;
            default: ++other; break;
        }
    }
};

// The failed attempts per net id, for at most kMaxTrackedIds ids (the oldest is forgotten when it is full).
class Attempts {
public:
    // A failed attempt of `id`: how many have failed so far, this one included (1 = the first, which is the one to log).
    uint32_t failed(uint16_t id) {
        auto found = find(id);
        if (found != ids_.end()) return ++found->second;
        if (ids_.size() == kMaxTrackedIds) ids_.pop_front();
        ids_.emplace_back(id, 1);
        return 1;
    }

    // `id` was created: how many attempts failed before (0 = it never failed). The id is forgotten.
    uint32_t created(uint16_t id) {
        auto found = find(id);
        if (found == ids_.end()) return 0;
        const uint32_t before = found->second;
        ids_.erase(found);
        return before;
    }

private:
    using Entry = std::pair<uint16_t, uint32_t>;

    std::deque<Entry>::iterator find(uint16_t id) {
        return std::find_if(ids_.begin(), ids_.end(), [id](const Entry& entry) { return entry.first == id; });
    }

    std::deque<Entry> ids_;
};

}  // namespace vehicle_trace_rules
