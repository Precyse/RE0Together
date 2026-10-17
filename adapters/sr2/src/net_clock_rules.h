#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// The pure rules of the guest's net clock (milestone 11 B1, unit tested). Every replicated object, players and NPCs
// alike, plays its input history back against the session's net clock T. The game sets T from every `clock` message,
// forward or backward, and a step back of D milliseconds replays the last D milliseconds of every history: each replayed
// release and press re-arms a gun, so remote guns fire faster than their refire. Here a guest's T only moves forward: a
// clock that lies behind T becomes a debt that is paid by letting T advance at half speed, and only the host's clock
// counts.
namespace net_clock_rules {

constexpr uint32_t kMaxClockDebtMs = 1000;  // a clock further behind than this is a real resync, set as the game does
constexpr uint32_t kMaxTickAdvanceMs = 250;  // the game caps the frame time it adds to T at 250
constexpr uint32_t kCatchUpDivisor = 2;      // a tick gives back at most half of the time it added
constexpr size_t kMinRemoteMembers = 2;      // with one remote the game's own assignment stays

// The rule governs a guest with two or more remote members. The host never receives a clock, and with one remote the
// shipped game's behaviour is kept.
inline bool governs(bool isHost, size_t remoteMembers) { return !isHost && remoteMembers >= kMinRemoteMembers; }

enum class Outcome {
    Forward,   // the clock is ahead of T: T takes it, as the game does
    Absorbed,  // the clock is behind T: T stays, the difference is the debt
    Resynced,  // the clock is further behind than the cap: T takes it, as the game does
    Ignored,   // not the host's clock
};

struct Clock {
    uint32_t time;
    uint32_t debt;
    Outcome outcome;
};

// A clock message: `target` is the sender's payload plus half its round trip time. The debt is measured again against the
// current T, so a new message replaces the old debt.
inline Clock onClock(uint32_t time, uint32_t target, bool fromHost, uint32_t debt) {
    if (!fromHost) return {time, debt, Outcome::Ignored};
    if (target >= time) return {target, 0, Outcome::Forward};
    const uint32_t behind = time - target;
    if (behind > kMaxClockDebtMs) return {target, 0, Outcome::Resynced};
    return {time, behind, Outcome::Absorbed};
}

// The events worth a line in the log outside trace=1: a clock from the wrong sender and a real resync.
inline bool needsLog(Outcome outcome) { return outcome == Outcome::Ignored || outcome == Outcome::Resynced; }

// What the game's tick added to T: the frame time, never more than the cap. Anything else (no tick ran, or T was set by a
// clock meanwhile) is not a tick's advance and counts as none.
inline uint32_t tickAdvance(uint32_t before, uint32_t after) {
    const uint32_t advance = after - before;
    return advance <= kMaxTickAdvanceMs ? advance : 0;
}

struct Tick {
    uint32_t time;
    uint32_t debt;
};

// After a tick that advanced T by `advance`: pays the debt with at most half of that advance.
inline Tick onTick(uint32_t time, uint32_t debt, uint32_t advance) {
    const uint32_t take = std::min(debt, advance / kCatchUpDivisor);
    return {time - take, debt - take};
}

}  // namespace net_clock_rules
