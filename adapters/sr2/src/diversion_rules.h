#pragma once
#include <cstddef>
#include <cstdint>

#include "activity_rules.h"
#include "partner_rules.h"
#include "sr2_addresses.h"

// The pure rules of milestone 16 wave 2, diversions and activities (no game access, unit tested): who the other participant
// of a two player activity is, the per-slot state of the players the game keeps one field for, and the geometry and logging
// rules of the thunks in diversion_*.cpp.
namespace diversion_rules {

constexpr size_t kSlots = 8;  // session slots 0..7
constexpr size_t kNone = partner_rules::kNone;

// An activity with two participants and spectators (a "2+S" activity): the one running is not an all-players type, or the
// co-op race diversion runs. -1 (no activity) is none.
inline bool isTwoPlusS(int32_t type, bool coopRace) {
    return coopRace || (type >= 0 && !activity_rules::isAllPlayersType(type));
}

// The handle of the human that is "the partner" of the local player in a 2+S activity: the other of the pair (owner,
// partner). 0 when it is not 2+S, the local player does not take part, or the pair is one handle.
inline uint32_t participantPin(uint32_t local, uint32_t owner, uint32_t partner, bool twoPlusS) {
    if (!twoPlusS || !local || owner == partner) return 0;
    if (local == owner) return partner;
    if (local == partner) return owner;
    return 0;
}

// The index of the first remote with a human that satisfies `pred(remote)`, the default one first, then in pairing order;
// kNone when none does.
template <typename Pred>
size_t findFirst(const partner_rules::Remote* remotes, size_t count, size_t defaultIndex, Pred pred) {
    if (defaultIndex < count && remotes[defaultIndex].human && pred(remotes[defaultIndex])) return defaultIndex;
    for (size_t i = 0; i < count; ++i) {
        if (i != defaultIndex && remotes[i].human && pred(remotes[i])) return i;
    }
    return kNone;
}

// findFirst, but `defaultIndex` when none satisfies `pred`.
template <typename Pred>
size_t firstMatching(const partner_rules::Remote* remotes, size_t count, size_t defaultIndex, Pred pred) {
    const size_t found = findFirst(remotes, count, defaultIndex, pred);
    return found == kNone ? defaultIndex : found;
}

// A value per session slot together with the human it was stored for: a new human in the slot reads empty, so a member who
// left needs no clean-up.
template <typename T>
class SlotTable {
public:
    void set(size_t slot, uintptr_t human, const T& value) {
        if (slot < kSlots) entries_[slot] = {human, value, true};
    }

    const T* get(size_t slot, uintptr_t human) const {
        if (slot >= kSlots || !entries_[slot].used || entries_[slot].human != human) return nullptr;
        return &entries_[slot].value;
    }

    T valueOr(size_t slot, uintptr_t human, const T& fallback) const {
        const T* value = get(slot, human);
        return value ? *value : fallback;
    }

    void clear(size_t slot) {
        if (slot < kSlots) entries_[slot].used = false;
    }

    void clearAll() {
        for (Entry& entry : entries_) entry.used = false;
    }

private:
    struct Entry {
        uintptr_t human;
        T value;
        bool used;
    };
    Entry entries_[kSlots] = {};
};

// Insurance Fraud team hits: the last object each remote hit. The teammate of the local player is the first remote whose
// last hit is the local player's (the default partner first); a zero hit never matches.
class TeamHits {
public:
    void record(size_t slot, uintptr_t human, uint32_t object) { last_.set(slot, human, object); }
    uint32_t lastOf(size_t slot, uintptr_t human) const { return last_.valueOr(slot, human, 0); }
    void clear() { last_.clearAll(); }

    size_t teammate(const partner_rules::Remote* remotes, size_t count, size_t defaultIndex, uint32_t localHit) const {
        if (!localHit) return kNone;
        return findFirst(remotes, count, defaultIndex,
                         [&](const partner_rules::Remote& remote) { return lastOf(remote.slot, remote.human) == localHit; });
    }

private:
    SlotTable<uint32_t> last_;
};

// What the game's count becomes when `delivered` of `count` are handed in (0x657C73 to 0x657C7F: x = x > 0 ? x : 0).
inline int32_t afterDelivery(int32_t count, int32_t delivered) {
    const int32_t left = count - delivered;
    return left > 0 ? left : 0;
}

// The Snatch carried count per remote. The default partner's count is the game's own field (read-through), so what the
// shipped game does for one remote is unchanged; only the other remotes use the table.
class CarryCounts {
public:
    int32_t get(size_t slot, uintptr_t human, bool isDefault, const int32_t& field) const {
        return isDefault ? field : others_.valueOr(slot, human, 0);
    }

    void set(size_t slot, uintptr_t human, bool isDefault, int32_t& field, int32_t value) {
        if (isDefault) {
            field = value;
        } else {
            others_.set(slot, human, value);
        }
    }

    void deliver(size_t slot, uintptr_t human, bool isDefault, int32_t& field, int32_t delivered) {
        set(slot, human, isDefault, field, afterDelivery(get(slot, human, isDefault, field), delivered));
    }

    void clearAll(int32_t& field) {
        field = 0;
        others_.clearAll();
    }

private:
    SlotTable<int32_t> others_;
};

// The slots whose player streaks, one bit each. The game's own byte is "someone streaks".
class StreakMask {
public:
    void set(size_t slot, bool on) {
        if (slot >= kSlots) return;
        bits_ = on ? (bits_ | bit(slot)) : (bits_ & ~bit(slot));
    }
    bool isOn(size_t slot) const { return slot < kSlots && (bits_ & bit(slot)) != 0; }
    uint8_t gameByte() const { return bits_ != 0 ? 1 : 0; }
    void clear() { bits_ = 0; }

private:
    static uint32_t bit(size_t slot) { return uint32_t{1} << slot; }
    uint32_t bits_ = 0;
};

// Per-slot copies of one game field that always holds the canonical slot's value (the default partner's). To run the game's
// code for another remote, `enter` saves the cell for the canonical slot and loads that remote's copy; `leave` stores the
// remote's copy and loads the canonical one back. When the canonical slot changes, `follow` moves the cell first. With one
// remote the canonical slot is that remote and nothing is ever swapped.
template <typename T>
class SwapTable {
public:
    void follow(size_t slot, uintptr_t human, T& cell) {
        if (known_ && slot_ == slot && human_ == human) return;
        if (known_) table_.set(slot_, human_, cell);
        load(slot, human, cell);
        known_ = true;
        slot_ = slot;
        human_ = human;
    }

    bool isCanonical(size_t slot, uintptr_t human) const { return known_ && slot_ == slot && human_ == human; }

    void enter(size_t slot, uintptr_t human, T& cell) {
        table_.set(slot_, human_, cell);
        load(slot, human, cell);
    }

    void leave(size_t slot, uintptr_t human, T& cell) {
        table_.set(slot, human, cell);
        load(slot_, human_, cell);
    }

    // The value a remote has now: the cell for the canonical slot, its copy for the others.
    T valueOf(size_t slot, uintptr_t human, const T& cell) const {
        return isCanonical(slot, human) ? cell : table_.valueOr(slot, human, T{});
    }

    // Forgets every copy and the canonical slot; the cell is left as it is.
    void forget() {
        table_.clearAll();
        known_ = false;
    }

private:
    void load(size_t slot, uintptr_t human, T& cell) const { cell = table_.valueOr(slot, human, T{}); }

    SlotTable<T> table_;
    bool known_ = false;
    size_t slot_ = 0;
    uintptr_t human_ = 0;
};

// The Snatch drop-off marker a player is tested against: the level has two, the local player's and the remote side's (every
// remote shares the second).
inline size_t deliveryMarker(size_t rank) { return rank == 0 ? 0 : 1; }

// A player delivers when it is no further than `radius` from the marker (0x404A20's float arithmetic: squared distance).
inline bool inDeliveryRange(const partner_rules::Vec3& player, const partner_rules::Vec3& marker, float radius) {
    const float dx = player.x - marker.x;
    const float dy = player.y - marker.y;
    const float dz = player.z - marker.z;
    const float distance = dx * dx + dy * dy + dz * dz;
    return distance <= radius * radius;
}

// Phase B: the Snatch spawn candidate that is farthest from its nearest player (largest smallest squared distance). Ties go
// to the first candidate. kNone without candidates.
inline size_t bestSpawn(const partner_rules::Vec3* candidates, size_t candidateCount, const partner_rules::Vec3* players,
                        size_t playerCount) {
    size_t best = kNone;
    float bestNearest = 0;
    for (size_t c = 0; c < candidateCount; ++c) {
        float nearest = 0;
        bool any = false;
        for (size_t p = 0; p < playerCount; ++p) {
            const float dx = candidates[c].x - players[p].x;
            const float dy = candidates[c].y - players[p].y;
            const float dz = candidates[c].z - players[p].z;
            const float distance = dx * dx + dy * dy + dz * dz;
            if (!any || distance < nearest) nearest = distance;
            any = true;
        }
        if (best == kNone || nearest > bestNearest) {
            best = c;
            bestNearest = nearest;
        }
    }
    return best;
}

// A decision that is asked every frame is logged when it changes: the first answer of each subject and every change.
template <size_t N>
class EdgeLog {
public:
    bool changed(size_t subject, bool answer) {
        if (subject >= N) return false;
        if (known_[subject] && last_[subject] == answer) return false;
        known_[subject] = true;
        last_[subject] = answer;
        return true;
    }

private:
    bool known_[N] = {};
    bool last_[N] = {};
};

}  // namespace diversion_rules
