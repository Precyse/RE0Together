// The downed, revive and respawn wait rules of milestone 16 W2 (no game): the game's deadline arithmetic, who can revive, the kill of a
// downed local, the wipe on a remote's or the local's death, the per-slot waits, the revive anchor and the blip marker.
#include <cstdio>

#include "../src/revive_rules.h"
#include "check.h"

namespace {

using partner_rules::kNone;
using partner_rules::Vec3;
using revive_rules::Player;

constexpr int32_t kWrap = sr2::kClockWrapMs;
constexpr int32_t kHalf = sr2::kClockHalfMs;
constexpr int32_t kWait = sr2::kRespawnWaitMs;
constexpr uint32_t kOneBits = 0x3F800000;
constexpr uint32_t kNegativeZeroBits = 0x80000000;
constexpr uint32_t kQuietNanBits = 0x7FC00000;
constexpr uintptr_t kDead = 0xD0;
constexpr uintptr_t kThird = 0xD1;

Player up(uintptr_t human, uint8_t slot) { return {human, slot, true, false, false}; }
Player downed(uintptr_t human, uint8_t slot) { return {human, slot, true, true, false}; }
Player dead(uintptr_t human, uint8_t slot) { return {human, slot, false, false, false}; }
Player waiting(uintptr_t human, uint8_t slot) { return {human, slot, false, false, true}; }

void theDeadlineArithmetic() {
    check(revive_rules::deadlineAfter(1000, kWait) == 1000 + kWait, "a deadline below the wrap is the sum");
    check(revive_rules::deadlineAfter(kWrap - kWait, kWait) == kWrap, "a sum equal to the wrap stays (the game compares jle)");
    check(revive_rules::deadlineAfter(kWrap - kWait + 1, kWait) == 1, "a sum above the wrap loses one wrap");
    check(revive_rules::deadlineAfter(kWrap, kWait) == kWait, "the clock at the wrap");
}

void theDeadlineTest() {
    constexpr int32_t kNow = 5000;
    check(!revive_rules::isPending(-1, kNow), "no deadline is not pending");
    check(revive_rules::isPending(kNow + kWait, kNow), "a deadline ahead is pending");
    check(!revive_rules::isPending(kNow - 1, kNow), "a deadline just passed is not pending");
    check(!revive_rules::isPending(kNow, kNow), "a deadline reached now is not pending");
    check(revive_rules::isPending(kNow + kHalf - 1, kNow), "a deadline less than half a wrap ahead is pending");
    check(!revive_rules::isPending(kNow + kHalf, kNow), "a deadline half a wrap ahead was set before the wrap: passed");
    constexpr int32_t kLate = kWrap - 1000;
    const int32_t wrapped = revive_rules::deadlineAfter(kLate, kWait);
    check(wrapped < kLate && revive_rules::isPending(wrapped, kLate), "a deadline that wrapped past the clock is still pending");
    check(!revive_rules::isPending(wrapped, wrapped + 1), "and passed once the clock followed");
    check(!revive_rules::isPending(kLate, 100), "a deadline far behind the wrapped clock has passed");
}

void whoCanRevive() {
    check(revive_rules::canRevive(up(1, 1)), "alive and standing can revive");
    check(!revive_rules::canRevive(downed(1, 1)), "a downed player cannot");
    check(!revive_rules::canRevive(dead(1, 1)), "a dead player cannot (its downed timer is -1 too)");
    check(revive_rules::makePlayer(1, 1, kOneBits, -1, false).alive, "positive health is alive");
    check(revive_rules::makePlayer(1, 1, kQuietNanBits, -1, false).alive, "a NaN health is alive, as the game's compare sees it");
    check(!revive_rules::makePlayer(1, 1, kNegativeZeroBits, -1, false).alive, "-0.0 is not alive");
    check(!revive_rules::makePlayer(1, 1, 0, -1, false).alive, "0.0 is not alive");
    check(revive_rules::makePlayer(1, 1, kOneBits, 0, false).down, "a downed timer of 0 is down");
    check(!revive_rules::makePlayer(1, 1, kOneBits, -1, false).down, "a timer of -1 is not down");
    const Player two[] = {dead(kDead, 1), downed(kThird, 2)};
    check(!revive_rules::anotherCanRevive(two, 2, kDead) && !revive_rules::anotherCanRevive(two, 2, 0), "nobody up: nobody can revive");
    const Player three[] = {dead(kDead, 1), up(kThird, 2)};
    check(revive_rules::anotherCanRevive(three, 2, kDead) && !revive_rules::anotherCanRevive(three, 2, kThird), "the excluded one does not count");
}

void killingTheDownedLocal() {
    const Player one[] = {dead(kDead, 1)};
    check(revive_rules::killDownedLocal(true, one, 1, kDead), "one remote: its death kills the downed local, as shipped");
    check(!revive_rules::killDownedLocal(false, one, 1, kDead), "a local that is not down is not killed");
    const Player thirdUp[] = {dead(kDead, 1), up(kThird, 2)};
    check(!revive_rules::killDownedLocal(true, thirdUp, 2, kDead), "a third player standing keeps the local bleeding");
    const Player thirdDown[] = {dead(kDead, 1), downed(kThird, 2)};
    check(revive_rules::killDownedLocal(true, thirdDown, 2, kDead), "a third player down cannot revive: kill");
    const Player thirdDead[] = {dead(kDead, 1), dead(kThird, 2)};
    check(revive_rules::killDownedLocal(true, thirdDead, 2, kDead), "a third player dead cannot revive: kill");
}

void wipingOnARemoteDeath() {
    const Player one[] = {dead(kDead, 1)};
    check(revive_rules::wipeOnRemoteDeath(true, one, 1, kDead) && !revive_rules::wipeOnRemoteDeath(false, one, 1, kDead),
          "one remote: the wipe is the local's wait, as shipped");
    const Player thirdUp[] = {dead(kDead, 1), up(kThird, 2)};
    check(!revive_rules::wipeOnRemoteDeath(true, thirdUp, 2, kDead), "a third player up: no wipe");
    const Player thirdDown[] = {dead(kDead, 1), downed(kThird, 2)};
    check(revive_rules::wipeOnRemoteDeath(true, thirdDown, 2, kDead), "a third player down: wipe");
    const Player thirdWaiting[] = {dead(kDead, 1), waiting(kThird, 2)};
    check(revive_rules::wipeOnRemoteDeath(true, thirdWaiting, 2, kDead), "a third player waiting to respawn: wipe");
    check(!revive_rules::wipeOnRemoteDeath(false, thirdWaiting, 2, kDead), "the local still has to wait");
}

void wipingOnTheLocalDeath() {
    check(!revive_rules::wipeOnLocalDeath(nullptr, 0), "no remote: no wipe");
    const Player waitingOne[] = {waiting(1, 1)};
    check(revive_rules::wipeOnLocalDeath(waitingOne, 1), "one remote waiting: wipe");
    const Player downedOne[] = {downed(1, 1)};
    check(!revive_rules::wipeOnLocalDeath(downedOne, 1), "one remote only down: no wipe, as shipped");
    const Player oneOfTwo[] = {waiting(1, 1), downed(2, 2)};
    check(!revive_rules::wipeOnLocalDeath(oneOfTwo, 2), "two remotes, one waiting: no wipe");
    const Player both[] = {waiting(1, 1), waiting(2, 2)};
    check(revive_rules::wipeOnLocalDeath(both, 2), "both waiting: wipe");
}

void theTeamGoingDown() {
    const Player one[] = {downed(kDead, 1)};
    check(revive_rules::teamDownOnRemoteDown(one, 1, kDead), "one remote: its downed message is the team down, as shipped");
    const Player otherUp[] = {downed(kDead, 1), up(kThird, 2)};
    check(!revive_rules::teamDownOnRemoteDown(otherUp, 2, kDead), "another remote up: not the team down");
    const Player otherDown[] = {downed(kDead, 1), downed(kThird, 2)};
    check(revive_rules::teamDownOnRemoteDown(otherDown, 2, kDead), "another remote down: the team down");
    const Player otherDead[] = {downed(kDead, 1), dead(kThird, 2)};
    check(revive_rules::teamDownOnRemoteDown(otherDead, 2, kDead), "another remote dead: the team down");
}

void theRespawnWaits() {
    revive_rules::RespawnWaits waits;
    constexpr int32_t kNow = 7000;
    check(!waits.waiting(1, kNow), "a new table waits for nobody");
    waits.record(1, kNow + kWait);
    check(waits.waiting(1, kNow) && !waits.waiting(2, kNow), "a recorded slot waits, another does not");
    check(!waits.waiting(1, kNow + kWait), "the wait ends at its deadline");
    waits.record(2, kNow + 2 * kWait);
    check(waits.waiting(1, kNow) && waits.waiting(2, kNow + kWait - 1), "two slots are independent");
    waits.clear();
    check(!waits.waiting(1, kNow) && !waits.waiting(2, kNow), "clear forgets every slot");
    waits.record(revive_rules::kWaitSlots, kNow + kWait);
    check(!waits.waiting(revive_rules::kWaitSlots, kNow), "a slot past the table is ignored");

    waits.record(1, kNow + kWait);
    waits.record(2, kNow + kWait);
    check(waits.forget(1) && !waits.waiting(1, kNow) && waits.waiting(2, kNow), "forget drops one slot and keeps the others");
    check(!waits.forget(1), "a slot without a deadline reports nothing forgotten");
    check(!waits.forget(revive_rules::kWaitSlots), "a slot past the table reports nothing forgotten");
}

void theRevivePoseAnchor() {
    const Vec3 positions[] = {{1, 0, 0}, {5, 0, 0}, {50, 0, 0}};
    const Vec3 origin{0, 0, 0};
    const bool none[] = {false, false, false};
    check(revive_rules::anchorIndex(positions, none, 3, origin) == kNone, "all in a ragdoll: none");
    check(revive_rules::anchorIndex(positions, none, 0, origin) == kNone, "no remote: none");
    const bool farOnly[] = {false, false, true};
    check(revive_rules::anchorIndex(positions, farOnly, 3, origin) == 2, "one upright far, the near ones ragdolled: the far one");
    const bool all[] = {true, true, true};
    check(revive_rules::anchorIndex(positions, all, 3, origin) == 0, "all upright: the nearest");
    const Vec3 tied[] = {{3, 0, 0}, {0, 3, 0}};
    check(revive_rules::anchorIndex(tied, all, 2, origin) == 0, "a tie goes to the lower index");
    const bool second[] = {false, true, true};
    check(revive_rules::anchorIndex(positions, second, 3, origin) == 1, "the index is of the whole list, not of the upright ones");
}

void theBlipMarker() {
    check(revive_rules::markerIndex(0) == kNone, "no marker");
    check(revive_rules::markerIndex(0x3B) == 0x3B, "the last record");
    check(revive_rules::markerIndex(0x3C) == kNone, "one past the table");
    check(revive_rules::markerIndex(0x20000 | 0x05) == 5, "high bits are not part of the index");
    check(revive_rules::markerIndex(0x2000) == 0, "a marker with only high bits set names record 0");
}

void theTraceText() {
    const Player remotes[] = {dead(1, 1), downed(2, 2), waiting(3, 3)};
    check(revive_rules::playersText(remotes, 3) == "slot 1 alive 0 down 0 waiting 0, slot 2 alive 1 down 1 waiting 0, slot 3 alive 0 down 0 waiting 1",
          "the players text");
    check(revive_rules::slotOf(remotes, 2, 2) == 2 && revive_rules::slotOf(remotes, 2, 9) == partner_rules::kNoSlot, "the slot of a remote, kNoSlot for another");
}

}  // namespace

int main() {
    theDeadlineArithmetic();
    theDeadlineTest();
    whoCanRevive();
    killingTheDownedLocal();
    wipingOnARemoteDeath();
    wipingOnTheLocalDeath();
    theTeamGoingDown();
    theRespawnWaits();
    theRevivePoseAnchor();
    theBlipMarker();
    theTraceText();
    return g_failures == 0 ? 0 : 1;
}
