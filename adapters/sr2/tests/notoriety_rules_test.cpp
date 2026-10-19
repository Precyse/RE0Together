// The guest notoriety lock rules of milestone 16 W2 (no game): the merge of the guests' locks and the per-guest store that replaces the
// game's single array.
#include <cstdio>

#include "../src/notoriety_rules.h"

namespace {

using notoriety_rules::GuestLocks;
using notoriety_rules::kNoLock;
using notoriety_rules::LockKind;

constexpr size_t kTeam = 1;
constexpr size_t kIndex = 0;
constexpr size_t kSlotA = 2;
constexpr size_t kSlotB = 3;
constexpr size_t kSlotGone = 5;
const uint8_t kBoth[] = {kSlotA, kSlotB};
const uint8_t kOnlyA[] = {kSlotA};

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void theMerge() {
    const int32_t none[] = {kNoLock, kNoLock};
    check(notoriety_rules::merged(none, 2) == kNoLock && notoriety_rules::merged(none, 0) == kNoLock, "no value set: no lock");
    const int32_t one[] = {kNoLock, 4};
    check(notoriety_rules::merged(one, 2) == 4, "one value set: that value");
    const int32_t threeAndOne[] = {3, 1};
    check(notoriety_rules::merged(threeAndOne, 2) == 2, "3 and 1 give 2");
    const int32_t threeAndTwo[] = {3, 2};
    check(notoriety_rules::merged(threeAndTwo, 2) == 3, "3 and 2 give 3: the mean rounds half up");
    const int32_t zeroes[] = {0, 0};
    check(notoriety_rules::merged(zeroes, 2) == 0, "a lock of 0 is a value");
}

// One game slot standing for the array: the store returns what the game's array holds afterwards.
struct Game {
    int32_t word = kNoLock;
    GuestLocks locks;

    notoriety_rules::Stored store(LockKind kind, size_t slot, int32_t value, const uint8_t* live, size_t liveCount) {
        const notoriety_rules::Stored stored = locks.store(kind, kTeam, kIndex, slot, value, word, live, liveCount);
        word = stored.merged;
        return stored;
    }
};

void oneRemoteIsTheShippedStore() {
    Game game;
    check(game.store(LockKind::Min, kSlotA, 3, kOnlyA, 1).merged == 3, "one guest: its lock goes in as it is");
    check(game.store(LockKind::Min, kSlotA, 1, kOnlyA, 1).merged == 1, "one guest: a new lock replaces it");
    check(game.store(LockKind::Min, kSlotA, kNoLock, kOnlyA, 1).merged == kNoLock, "one guest: clearing it clears the lock");
    check(game.store(LockKind::Min, kSlotA, 0, kOnlyA, 1).merged == 0, "one guest: a lock of 0");
}

void twoGuestsKeepTheirOwnLocks() {
    Game game;
    check(game.store(LockKind::Min, kSlotA, 3, kBoth, 2).merged == 3, "guest A sets 3");
    check(game.store(LockKind::Min, kSlotB, kNoLock, kBoth, 2).merged == 3, "guest B clearing its lock leaves A's");
    Game other;
    other.store(LockKind::Min, kSlotB, 2, kBoth, 2);
    const notoriety_rules::Stored cleared = other.store(LockKind::Min, kSlotA, kNoLock, kBoth, 2);
    check(cleared.merged == 2 && cleared.set == 1, "guest A clearing its lock while B holds 2 leaves 2, one set");
    Game both;
    both.store(LockKind::Min, kSlotA, 3, kBoth, 2);
    const notoriety_rules::Stored mean = both.store(LockKind::Min, kSlotB, 1, kBoth, 2);
    check(mean.merged == 2 && mean.set == 2, "two locks merge to their mean, two set");
}

void aLeftGuestIsIgnored() {
    Game game;
    game.store(LockKind::Min, kSlotGone, 4, kBoth, 2);
    check(game.store(LockKind::Min, kSlotA, 2, kOnlyA, 1).merged == 2, "a slot that is not a live member does not count");
}

void theGamesResetDropsOldLocks() {
    Game game;
    game.store(LockKind::Min, kSlotA, 3, kBoth, 2);
    game.store(LockKind::Min, kSlotB, 3, kBoth, 2);
    game.word = kNoLock;
    const notoriety_rules::Stored stored = game.store(LockKind::Min, kSlotB, 1, kBoth, 2);
    check(stored.merged == 1 && stored.columnReset, "after the game reset the array only the new lock stands, the reset is reported");
    check(!game.store(LockKind::Min, kSlotA, 1, kBoth, 2).columnReset, "the next store of the column is not a reset");
}

void kindsAndColumnsAreIndependent() {
    GuestLocks locks;
    int32_t minWord = kNoLock;
    int32_t maxWord = kNoLock;
    minWord = locks.store(LockKind::Min, kTeam, kIndex, kSlotA, 3, minWord, kBoth, 2).merged;
    maxWord = locks.store(LockKind::Max, kTeam, kIndex, kSlotB, 5, maxWord, kBoth, 2).merged;
    check(minWord == 3 && maxWord == 5, "min and max do not share values");
    int32_t otherTeam = kNoLock;
    otherTeam = locks.store(LockKind::Min, kTeam + 1, kIndex, kSlotB, 4, otherTeam, kBoth, 2).merged;
    int32_t otherIndex = kNoLock;
    otherIndex = locks.store(LockKind::Min, kTeam, kIndex + 1, kSlotB, 1, otherIndex, kBoth, 2).merged;
    check(otherTeam == 4 && otherIndex == 1, "teams and lock indexes do not share values");
    check(locks.store(LockKind::Min, kTeam, kIndex, kSlotA, 3, minWord, kBoth, 2).merged == 3, "the first column is as it was");
}

void aRequestOutsideTheTableIsReturnedAsItIs() {
    GuestLocks locks;
    check(locks.store(LockKind::Min, notoriety_rules::kTeams, kIndex, kSlotA, 3, kNoLock, kBoth, 2).merged == 3, "a team past the table");
    check(locks.store(LockKind::Min, kTeam, notoriety_rules::kIndexes, kSlotA, 3, kNoLock, kBoth, 2).merged == 3, "an index past the table");
    check(locks.store(LockKind::Max, kTeam, kIndex, notoriety_rules::kSlots, 3, kNoLock, kBoth, 2).merged == 3, "a slot past the table");
}

}  // namespace

int main() {
    theMerge();
    oneRemoteIsTheShippedStore();
    twoGuestsKeepTheirOwnLocks();
    aLeftGuestIsIgnored();
    theGamesResetDropsOldLocks();
    kindsAndColumnsAreIndependent();
    aRequestOutsideTheTableIsReturnedAsItIs();
    return g_failures == 0 ? 0 : 1;
}
