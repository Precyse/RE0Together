#include "resync.h"

#include <algorithm>
#include <cstring>

#include "reject_counters.h"

namespace {

struct Waiting {
    uint8_t slot;
    uint32_t scopes;
};

std::vector<Waiting> g_waiting;  // net thread only

void add(uint8_t slot, uint32_t scopes) {
    if (scopes == 0) return;
    const auto found = std::find_if(g_waiting.begin(), g_waiting.end(), [slot](const Waiting& w) { return w.slot == slot; });
    if (found != g_waiting.end()) {
        found->scopes |= scopes;
    } else {
        g_waiting.push_back({slot, scopes});
    }
}

}  // namespace

namespace resync {

void requestLocal(uint32_t scopes) { add(proto::kSlotAll, scopes); }

void onFrame(const GameFrame& frame) {
    if (frame.type != kMsgResync) return;
    ResyncRequest decoded;
    if (frame.payload.size() != sizeof(decoded)) {
        reject_counters::count(kMsgResync, reject_counters::Reason::Malformed);
        return;
    }
    std::memcpy(&decoded, frame.payload.data(), sizeof(decoded));
    add(frame.slot, decoded.scopes & kAll);
}

std::vector<uint8_t> takeRequests(uint32_t scope) {
    std::vector<uint8_t> slots;
    for (Waiting& waiting : g_waiting) {
        if ((waiting.scopes & scope) == 0) continue;
        slots.push_back(waiting.slot);
        waiting.scopes &= ~scope;
    }
    std::erase_if(g_waiting, [](const Waiting& w) { return w.scopes == 0; });
    return slots;
}

}  // namespace resync
