#include "anim_event.h"

#include <algorithm>
#include <mutex>

#include "anim_wire.h"
#include "reject_counters.h"

namespace {

constexpr size_t kMaxHeld = 64;  // a peer sending more pulses than this inside the hold time is misbehaving

struct Held {
    uint8_t slot;
    remote_animation::Change change;
    TimeUs until;
};

std::mutex g_mutex;  // the net thread adds, the thread that evaluates the remote's animation reads
std::vector<Held> g_held;

}  // namespace

namespace anim_event {

void onFrame(const GameFrame& frame, TimeUs now) {
    if (frame.type != anim_wire::kMsgAnimEvent) return;
    anim_wire::Report report;
    if (!anim_wire::decode(frame.payload, report)) {
        reject_counters::count(frame.type, reject_counters::Reason::Malformed);
        return;
    }
    std::lock_guard lock(g_mutex);
    for (const remote_animation::Change& change : report.changes) {
        if (g_held.size() >= kMaxHeld) return;
        g_held.push_back({frame.slot, change, now + kHoldUs});
    }
}

std::vector<remote_animation::Change> active(uint8_t slot, TimeUs now) {
    std::lock_guard lock(g_mutex);
    std::erase_if(g_held, [now](const Held& held) { return held.until <= now; });
    std::vector<remote_animation::Change> out;
    for (const Held& held : g_held) {
        if (held.slot == slot) out.push_back(held.change);
    }
    return out;
}

}  // namespace anim_event
