#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "partner_rules.h"
#include "sr2_addresses.h"

// The pure rules of milestone 17 wave 3 (docs/SR2_NOTES.md, "Remote poses, world walks, garage door and client name (milestone 17
// wave 3)"; no game access, unit tested): the Ambulance pose of one human, which remotes get a follower bar, who sent the activity
// completion masks, and the trace texts.
namespace world_walks_rules {

constexpr int32_t kNoPose = static_cast<int32_t>(sr2::kNoAnimState);
constexpr size_t kMaxBars = sr2::kFollowerBarCount;
constexpr size_t kNone = partner_rules::kNone;
constexpr uint8_t kNoSlot = partner_rules::kNoSlot;
constexpr size_t kTextBytes = 128;

// The CPR pose `stored` for a human (the anim id the Ambulance diversion sent for it, kNoPose for none): 0x5F7DF0's rule on one
// human. The pose holds only while the human has no scripted action of its own in progress.
inline int32_t ambulancePose(int32_t stored, int32_t currentAction) {
    return stored != kNoPose && currentAction == kNoPose ? stored : kNoPose;
}

// The humans of `items` that can get a follower bar: the default remote first, then the others in pairing order; a remote without
// a human is skipped. At most kMaxBars are written to `out`; how many were.
inline size_t orderedRemotes(const partner_rules::Remote* items, size_t count, size_t defaultIndex, uintptr_t (&out)[kMaxBars]) {
    size_t written = 0;
    if (defaultIndex < count && items[defaultIndex].human) out[written++] = items[defaultIndex].human;
    for (size_t i = 0; i < count && written < kMaxBars; ++i) {
        if (i != defaultIndex && items[i].human) out[written++] = items[i].human;
    }
    return written;
}

// The partner and the three bar humans of the local player's HUD (0x797CD0's locals); 0 is no human.
struct FollowerBars {
    uintptr_t partner;
    uintptr_t bar[kMaxBars];
};

// The bars every remote can have: the partner and bar 1 are the first of `ordered`, bar 2 the second and, with three remotes,
// bar 0 the third. The game's own bars stand with fewer than two remotes and when the game found no partner.
inline FollowerBars followerBars(FollowerBars shipped, const uintptr_t* ordered, size_t n) {
    if (n < 2 || shipped.partner == 0) return shipped;
    FollowerBars bars = shipped;
    bars.partner = ordered[0];
    bars.bar[1] = ordered[0];
    bars.bar[2] = ordered[1];
    if (n >= kMaxBars) bars.bar[0] = ordered[2];
    return bars;
}

// The remote whose activity completion masks the host holds (the last client that sent misc 7).
struct CompletionSender {
    uint8_t slot = kNoSlot;
    uintptr_t human = 0;
};

// The index of the remote that is still `sender` (same slot and same human), kNone when it left, has a new human or none was remembered.
inline size_t completionSenderIndex(const partner_rules::Remote* items, size_t count, CompletionSender sender) {
    if (sender.human == 0) return kNone;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].slot == sender.slot && items[i].human == sender.human) return i;
    }
    return kNone;
}

// "0x1A2B 0x0 0x3C4D" for the three bar humans.
inline const char* barsText(const FollowerBars& bars, char (&out)[kTextBytes]) {
    snprintf(out, sizeof(out), "0x%X 0x%X 0x%X", static_cast<unsigned>(bars.bar[0]), static_cast<unsigned>(bars.bar[1]),
             static_cast<unsigned>(bars.bar[2]));
    return out;
}

// "slot 2 human 0x1A2B".
inline const char* slotText(uint8_t slot, uintptr_t human, char (&out)[kTextBytes]) {
    snprintf(out, sizeof(out), "slot %u human 0x%X", static_cast<unsigned>(slot), static_cast<unsigned>(human));
    return out;
}

}  // namespace world_walks_rules
