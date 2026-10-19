#pragma once
#include <cstddef>
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of milestone 4 (no game access, unit tested): which remote human belongs to which session member,
// which remote is "the partner" on every machine, and the answers the partner thunks give for several remotes.
namespace partner_rules {

constexpr size_t kMaxWalked = sr2::kMaxWalkedPlayers;
constexpr size_t kNone = static_cast<size_t>(-1);
constexpr uint8_t kNoSlot = 0xFF;

struct HumanEntry {
    uintptr_t human;
    uint8_t ownerSlot;  // the session slot of the machine that owns the human
};

struct MemberEntry {
    uintptr_t member;
    uint8_t slot;
    uint32_t state;
};

// One remote player: a member of the session and the human it owns. A joining member has no human yet.
struct Remote {
    uintptr_t human;
    uintptr_t member;
    uint8_t slot;
    uint32_t state;  // of the member
};

// `trusted` is false when a remote human's owner slot matches no member: the mapping cannot be used, and `items`
// holds the game's own answer, the first remote human and the first remote member of the lists.
struct Pairing {
    Remote items[kMaxWalked];
    size_t count;
    bool trusted;
};

struct Vec3 {
    float x, y, z;
};

// True when `human` is an object of the static player pool; any other human is an NPC.
inline bool isPlayerPoolMember(uintptr_t human) {
    return human >= sr2::kPlayerPool && human < sr2::kPlayerPool + sr2::kPlayerPoolCount * sr2::kPlayerPoolStride;
}

// Joins the remote humans to the remote members (both in list order) by slot, in ascending slot order.
inline void pair(const HumanEntry* humans, size_t humanCount, const MemberEntry* members, size_t memberCount,
                 Pairing& out) {
    out.count = 0;
    out.trusted = true;
    size_t matched = 0;
    for (size_t m = 0; m < memberCount && out.count < kMaxWalked; ++m) {
        Remote remote{0, members[m].member, members[m].slot, members[m].state};
        for (size_t h = 0; h < humanCount; ++h) {
            if (humans[h].ownerSlot != members[m].slot) continue;
            remote.human = humans[h].human;
            ++matched;
            break;
        }
        size_t at = out.count++;
        for (; at > 0 && out.items[at - 1].slot > remote.slot; --at) out.items[at] = out.items[at - 1];
        out.items[at] = remote;
    }
    if (matched == humanCount) return;
    out.trusted = false;
    out.count = (humanCount | memberCount) ? 1 : 0;
    if (out.count) {
        out.items[0] = {humanCount ? humans[0].human : 0, memberCount ? members[0].member : 0,
                        memberCount ? members[0].slot : kNoSlot, memberCount ? members[0].state : 0};
    }
}

// The remote every machine treats as the partner: on a guest the host's player, on the host the remote with the
// lowest slot. kNone without a remote human. With one remote it is that remote, as in the shipped game.
inline size_t pickDefault(const Pairing& pairing, uintptr_t hostMember, bool localIsHost) {
    size_t first = kNone;
    for (size_t i = 0; i < pairing.count; ++i) {
        if (!pairing.items[i].human) continue;
        if (first == kNone) first = i;
        if (!localIsHost && pairing.trusted && pairing.items[i].member == hostMember) return i;
    }
    return first;
}

// True when every remote member has a player human that is done joining. No remote member: ready.
template <typename IsReady>
bool allReady(const Pairing& pairing, IsReady isReady) {
    for (size_t i = 0; i < pairing.count; ++i) {
        const Remote& remote = pairing.items[i];
        if (remote.member && !(remote.human && isReady(remote.human))) return false;
    }
    return true;
}

// The member that is joining: the first remote member below `inGameState`, else kNone.
inline size_t firstJoining(const Pairing& pairing, uint32_t inGameState) {
    for (size_t i = 0; i < pairing.count; ++i) {
        if (pairing.items[i].member && pairing.items[i].state < inGameState) return i;
    }
    return kNone;
}

// A member left: true when no other remote member is in the game, so the mission and the co-op session end.
inline bool isLastInGame(const Pairing& pairing, uintptr_t leaving, uint32_t inGameState) {
    for (size_t i = 0; i < pairing.count; ++i) {
        const Remote& remote = pairing.items[i];
        if (remote.member && remote.member != leaving && remote.state == inGameState) return false;
    }
    return true;
}

// The index of the position closest to `point`, kNone without positions. Ties go to the earlier one.
inline size_t nearestIndex(const Vec3* positions, size_t count, const Vec3& point) {
    size_t best = kNone;
    float bestDistance = 0;
    for (size_t i = 0; i < count; ++i) {
        const float dx = positions[i].x - point.x;
        const float dy = positions[i].y - point.y;
        const float dz = positions[i].z - point.z;
        const float distance = dx * dx + dy * dy + dz * dz;
        if (best == kNone || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

}  // namespace partner_rules
