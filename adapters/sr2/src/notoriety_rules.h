#pragma once
#include <cstddef>
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of the guest notoriety locks (milestone 16 W2, unit tested; docs/SR2_NOTES.md "Downed, revive and notoriety (M16 W2)").
// The game keeps one lock array per team for all guests together, so with two or more guests the last writer wins; the host keeps one
// value per guest slot and puts the merged one into the game's array.
namespace notoriety_rules {

constexpr size_t kKinds = 2;
constexpr size_t kTeams = sr2::kNotorietyTeamCount;
constexpr size_t kIndexes = sr2::kNotorietyLockIndexes;
constexpr size_t kSlots = sr2::kMaxWalkedPlayers;
constexpr int32_t kNoLock = sr2::kNotorietyNoLock;

enum class LockKind : size_t { Min, Max };

// -1 when no value is set, else the mean of the set values rounded half up.
inline int32_t merged(const int32_t* values, size_t count) {
    int32_t sum = 0;
    int32_t set = 0;
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == kNoLock) continue;
        sum += values[i];
        ++set;
    }
    return set == 0 ? kNoLock : (sum + set / 2) / set;
}

// What a store left in the game's array: the merged value, how many guests have a value set, and whether the column was reset first.
struct Stored {
    int32_t merged;
    size_t set;
    bool columnReset;
};

// The locks the guests asked for, per kind, team, lock index and guest slot, and the last value put into the game's array.
class GuestLocks {
public:
    GuestLocks() {
        for (auto& kind : columns_) {
            for (auto& team : kind) {
                for (Column& column : team) column.reset();
            }
        }
    }

    // A guest in `slot` asked for `value`. `slotNow` is what the game's array holds. A different value than the last one written means
    // the game reset the column, so every earlier request is dropped. The result is the merged value over the slots in `liveSlots` for
    // the game's array; a request that names no column is returned as it is.
    Stored store(LockKind kind, size_t team, size_t index, size_t slot, int32_t value, int32_t slotNow, const uint8_t* liveSlots,
                 size_t liveCount) {
        const size_t kindIndex = static_cast<size_t>(kind);
        if (kindIndex >= kKinds || team >= kTeams || index >= kIndexes || slot >= kSlots) return {value, 0, false};
        Column& column = columns_[kindIndex][team][index];
        const bool reset = slotNow != column.written;
        if (reset) column.reset();
        column.values[slot] = value;
        int32_t live[kSlots];
        size_t count = 0;
        size_t set = 0;
        for (size_t i = 0; i < liveCount && count < kSlots; ++i) {
            if (liveSlots[i] >= kSlots) continue;
            live[count++] = column.values[liveSlots[i]];
            if (live[count - 1] != kNoLock) ++set;
        }
        column.written = merged(live, count);
        return {column.written, set, reset};
    }

private:
    struct Column {
        int32_t values[kSlots];
        int32_t written;

        void reset() {
            for (int32_t& value : values) value = kNoLock;
            written = kNoLock;
        }
    };

    Column columns_[kKinds][kTeams][kIndexes];
};

}  // namespace notoriety_rules
