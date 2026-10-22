#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "partner_rules.h"
#include "player_visuals_rules.h"
#include "sr2_addresses.h"

// The pure rules of milestone 16 W2 for downed players, revives and the respawn wait (no game access, unit tested; docs/SR2_NOTES.md
// "Downed, revive and notoriety (M16 W2)"). With one remote every rule gives the answer of the shipped game.
namespace revive_rules {

constexpr size_t kWaitSlots = sr2::kMaxWalkedPlayers;  // one respawn wait per remote session slot
constexpr int32_t kNoDeadline = -1;

// The game's deadline arithmetic on its clock in milliseconds (0x8CBC5B..0x8CBC72): the sum wraps once it is above the clock's wrap.
constexpr int32_t deadlineAfter(int32_t now, int32_t ms) {
    const int32_t deadline = now + ms;
    return deadline > sr2::kClockWrapMs ? deadline - sr2::kClockWrapMs : deadline;
}

// The game's test of a pending deadline (0x8CBDB8..0x8CBE04 and 0xBDD230): set, and not yet reached on the wrapping clock. A deadline
// more than half the wrap ahead of the clock was set before the clock wrapped and has passed; one that far behind is still ahead.
constexpr bool isPending(int32_t deadline, int32_t now) {
    if (deadline < 0) return false;
    if (deadline > now) return deadline - now < sr2::kClockHalfMs;
    return now - deadline >= sr2::kClockHalfMs;
}

// A remote player as the rules see it: `alive` is health above 0, `down` is a started bleed-out, `waiting` is a pending respawn wait.
struct Player {
    uintptr_t human;
    uint8_t slot;
    bool alive;
    bool down;
    bool waiting;
};

inline Player makePlayer(uintptr_t human, uint8_t slot, uint32_t healthBits, int32_t downedTimer, bool waiting) {
    return {human, slot, player_visuals_rules::healthIsPositive(healthBits), downedTimer >= 0, waiting};
}

// A downed player is revived by one who is alive and standing; the timer of a dead player is -1 too.
constexpr bool canRevive(const Player& player) { return player.alive && !player.down; }

// The session slot of the remote `human`, kNoSlot for a human that is not among them.
inline uint8_t slotOf(const Player* remotes, size_t count, uintptr_t human) {
    for (size_t i = 0; i < count; ++i) {
        if (remotes[i].human == human) return remotes[i].slot;
    }
    return partner_rules::kNoSlot;
}

// True when a remote other than `except` can revive.
inline bool anotherCanRevive(const Player* remotes, size_t count, uintptr_t except) {
    for (size_t i = 0; i < count; ++i) {
        if (remotes[i].human != except && canRevive(remotes[i])) return true;
    }
    return false;
}

// The death of `dead` while the local player bleeds out kills the local only when nobody else could still revive it.
inline bool killDownedLocal(bool localDown, const Player* remotes, size_t count, uintptr_t dead) {
    return localDown && !anotherCanRevive(remotes, count, dead);
}

// True when every remote other than `except` is down or waiting to respawn.
inline bool othersDownOrWaiting(const Player* remotes, size_t count, uintptr_t except) {
    for (size_t i = 0; i < count; ++i) {
        if (remotes[i].human != except && !remotes[i].down && !remotes[i].waiting) return false;
    }
    return true;
}

// The team is wiped when a remote dies while the local player waits to respawn and every other remote is down or waiting.
inline bool wipeOnRemoteDeath(bool localWaiting, const Player* remotes, size_t count, uintptr_t dead) {
    return localWaiting && othersDownOrWaiting(remotes, count, dead);
}

// The team is wiped when the local player dies while every remote waits to respawn (a remote that is only down does not count).
inline bool wipeOnLocalDeath(const Player* remotes, size_t count) {
    if (count == 0) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!remotes[i].waiting) return false;
    }
    return true;
}

// The downed message of `downed` is a team wipe when no other remote can revive.
inline bool teamDownOnRemoteDown(const Player* remotes, size_t count, uintptr_t downed) { return !anotherCanRevive(remotes, count, downed); }

// The respawn waits of the remotes (host only), one deadline per session slot; a slot past the table is ignored.
class RespawnWaits {
public:
    RespawnWaits() { clear(); }

    void record(size_t slot, int32_t deadline) {
        if (slot < kWaitSlots) deadlines_[slot] = deadline;
    }

    void clear() {
        for (int32_t& deadline : deadlines_) deadline = kNoDeadline;
    }

    bool waiting(size_t slot, int32_t now) const { return slot < kWaitSlots && isPending(deadlines_[slot], now); }

    // Drops the wait of a slot whose member left; true when the slot held a deadline.
    bool forget(size_t slot) {
        if (slot >= kWaitSlots || deadlines_[slot] == kNoDeadline) return false;
        deadlines_[slot] = kNoDeadline;
        return true;
    }

private:
    int32_t deadlines_[kWaitSlots];
};

// The index of the position closest to `point` among those with `upright` set, kNone without one. Ties go to the lower index.
inline size_t anchorIndex(const partner_rules::Vec3* positions, const bool* upright, size_t count, const partner_rules::Vec3& point) {
    partner_rules::Vec3 candidates[partner_rules::kMaxWalked];
    size_t source[partner_rules::kMaxWalked];
    size_t found = 0;
    for (size_t i = 0; i < count && found < partner_rules::kMaxWalked; ++i) {
        if (!upright[i]) continue;
        candidates[found] = positions[i];
        source[found++] = i;
    }
    const size_t best = partner_rules::nearestIndex(candidates, found, point);
    return best == partner_rules::kNone ? partner_rules::kNone : source[best];
}

// The index of a revive blip marker in the blip table, kNone for no marker or one past the table.
constexpr size_t markerIndex(uint32_t marker) {
    if (marker == 0) return partner_rules::kNone;
    const uint32_t index = marker & sr2::kBlipIndexMask;
    return index >= sr2::kBlipCount ? partner_rules::kNone : index;
}

// "slot 1 alive 1 down 0 waiting 0, slot 2 alive 0 down 1 waiting 1" for the trace lines.
inline std::string playersText(const Player* remotes, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        char part[48];
        std::snprintf(part, sizeof(part), "%sslot %u alive %d down %d waiting %d", text.empty() ? "" : ", ",
                      static_cast<unsigned>(remotes[i].slot), remotes[i].alive ? 1 : 0, remotes[i].down ? 1 : 0, remotes[i].waiting ? 1 : 0);
        text += part;
    }
    return text;
}

}  // namespace revive_rules
