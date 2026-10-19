#pragma once
#include <cstddef>
#include <cstdint>

#include "partner_rules.h"
#include "sr2_addresses.h"

// The pure rules of milestone 17 wave 3, Crowd Control for 3 or 4 players (no game access, unit tested): where an event keeps its
// navs, which remote holds the celebrity, which humans a call is made for, and the order the protect start walks them in.
namespace crowd_control_rules {

constexpr size_t kNone = partner_rules::kNone;

// The three navs of an event record: where the local player starts, where the partner starts and where the event ends.
enum class NavSide : uint8_t { Local, Remote, End };

// The offset, inside the level data at [level + 4], of the nav a side of an event uses: the game's
// (event * 0x5B2 + (i & 1)) * 4 + 0x60 for i = 0 (Local) and i = 1 (Remote).
constexpr uintptr_t navOffset(uint32_t event, NavSide side) {
    return event * sr2::kCrowdEventStride + sr2::kCrowdNavOffset + static_cast<uintptr_t>(side) * sizeof(uint32_t);
}

// The index of the first remote whose held handle is `handle`. A handle of 0 means "holds nothing" and matches no one.
inline size_t holderIndex(const uint32_t* held, size_t count, uint32_t handle) {
    if (handle == 0) return kNone;
    for (size_t i = 0; i < count; ++i) {
        if (held[i] == handle) return i;
    }
    return kNone;
}

// A warp whose human is a remote player goes to every remote; the local player, no human and an NPC keep the game's call.
constexpr bool warpsEveryRemote(bool humanIsRemotePlayer) { return humanIsRemotePlayer; }

constexpr size_t kMaxProtectOrder = partner_rules::kMaxWalked + 2;  // the local player, every remote, the celebrity

// The humans the protect start calls its vehicle function for, in the game's order with the remotes in the partner's place:
// the local player, every remote, the celebrity. Zeros are skipped. Returns how many were written to `out`.
inline size_t protectStartOrder(uintptr_t local, const uintptr_t* remotes, size_t remoteCount, uintptr_t celebrity,
                                uintptr_t (&out)[kMaxProtectOrder]) {
    size_t count = 0;
    if (local) out[count++] = local;
    for (size_t i = 0; i < remoteCount && count < kMaxProtectOrder - 1; ++i) {
        if (remotes[i]) out[count++] = remotes[i];
    }
    if (celebrity) out[count++] = celebrity;
    return count;
}

}  // namespace crowd_control_rules
