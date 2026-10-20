#pragma once
#include <cstdint>

#include "demo_skip_wire.h"

// Pure rules of the shared cutscene skip (no game access, unit tested): which demo is playing, which skip is waiting for
// it, and what a press does.
namespace demo_skip_rule {

// A skip that arrived before its demo reached the skip test (or while the scheduler is loading) waits this long.
constexpr int64_t kPendingMs = 10000;

// The demo the thread this machine runs started: `serialMine` as in event_sync's Slot, `leads` = this machine leads it.
struct Running {
    uint16_t demoId = 0;
    uint16_t serial = 0;
    bool serialMine = false;
    bool leads = false;
};

// A received skip names a demo and a thread (flags say whose serial); it applies to the running demo of that thread only.
constexpr bool matches(const Running& running, const demo_skip_wire::DemoSkip& skip) {
    return skip.demoId == running.demoId && skip.serial == running.serial &&
           (skip.flags == event_sync::kSerialIsReceivers) == running.serialMine;
}

// Wrap-safe age of a millisecond stamp.
constexpr int64_t ageMs(uint32_t stampMs, uint32_t nowMs) { return static_cast<int32_t>(nowMs - stampMs); }

// What a press at the skip test means here: the leader tells the follower, the follower asks the leader.
enum class Press : uint8_t { Ignore, SendSkip, SendRequest };

constexpr Press onPress(bool demoRunning, bool leads) {
    if (!demoRunning) return Press::Ignore;
    return leads ? Press::SendSkip : Press::SendRequest;
}

// The engine's own skip test in state 5 (0x54b496 .. 0x54b4f9) from the three inputs it reads.
constexpr bool skipInput(uint32_t padTriggerWord, uint32_t keyboardWord, uint8_t mouseButtons, uint32_t padSkipBit,
                         uint32_t keyboardMask, uint8_t mouseBit) {
    return (padTriggerWord & padSkipBit) != 0 || (keyboardWord & keyboardMask) != 0 || (mouseButtons & mouseBit) != 0;
}

// The running demo and the one skip waiting for it. Not thread safe: the owner locks.
class Tracker {
public:
    void start(const Running& running) {
        running_ = running;
        started_ = true;
    }
    void end() { started_ = false; }
    bool running() const { return started_; }
    const Running& current() const { return running_; }

    // Keeps the newest skip. A DEMO_SKIP comes from the leader and is for the follower, a DEMO_SKIP_REQUEST the other
    // way round, so `fromLeader` must be the opposite of the running demo's `leads`.
    void add(const demo_skip_wire::DemoSkip& skip, bool fromLeader, uint32_t nowMs) {
        pending_ = skip;
        pendingFromLeader_ = fromLeader;
        pendingMs_ = nowMs;
        hasPending_ = true;
    }

    // True once when the waiting skip belongs to the running demo and has not lapsed. A skip that lapses is dropped, so
    // one for a demo that never starts here cannot hit a later one.
    bool take(uint32_t nowMs) {
        if (!hasPending_) return false;
        if (ageMs(pendingMs_, nowMs) >= kPendingMs) {
            hasPending_ = false;
            return false;
        }
        if (!started_ || pendingFromLeader_ == running_.leads || !matches(running_, pending_)) return false;
        hasPending_ = false;
        return true;
    }

private:
    Running running_;
    bool started_ = false;
    demo_skip_wire::DemoSkip pending_{};
    uint32_t pendingMs_ = 0;
    bool pendingFromLeader_ = false;
    bool hasPending_ = false;
};

}  // namespace demo_skip_rule
