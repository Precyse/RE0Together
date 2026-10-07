// Checks the door barrier rule (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/room_gate_rule.h"

namespace {

namespace rule = room_gate_rule;
using rule::Verdict;

constexpr uint16_t kRoom = 0x2a;
constexpr uint16_t kOtherRoom = 0x2c;
constexpr uint16_t kLoading = 0xffff;
constexpr uint16_t kNoDoor = 0xffff;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

constexpr rule::Peer inSharedDoor(bool ready) { return {kLoading, kRoom, true, ready}; }

void testHoldsOnlyInTheSameSharedDoor() {
    check(rule::holdsAtFinish(true, kRoom, inSharedDoor(false)), "peer still in our door: hold");
    check(!rule::holdsAtFinish(true, kRoom, inSharedDoor(true)), "peer's door ready too: no hold");
    check(!rule::holdsAtFinish(false, kRoom, inSharedDoor(false)), "a door this machine took alone: never");
    check(!rule::holdsAtFinish(true, kRoom, {kLoading, kRoom, false, false}),
          "the partner heading into the same room through its own door: never");
    check(!rule::holdsAtFinish(true, kRoom, {kLoading, kOtherRoom, true, false}), "peer in a door elsewhere: never");
    check(!rule::holdsAtFinish(true, kRoom, {kRoom, kNoDoor, false, false}), "peer already in the room: no hold");
    check(!rule::holdsAtFinish(true, kRoom, {kOtherRoom, kNoDoor, false, false}), "peer not in a door: no hold");
}

void testSimultaneousFinish() {
    // Both report ready before checking; each sees the other's ready flag and goes on.
    check(!rule::holdsAtFinish(true, kRoom, inSharedDoor(true)), "the second to finish never waits");
    check(rule::check(kRoom, true, inSharedDoor(true), 10) == Verdict::PeerReady, "the first is released at once");
}

void testReleasesWhenThePeerIsReady() {
    check(rule::check(kRoom, true, inSharedDoor(false), 3000) == Verdict::Hold, "peer still loading: hold");
    check(rule::check(kRoom, true, inSharedDoor(true), 3000) == Verdict::PeerReady, "peer's door ready to finish");
    check(rule::check(kRoom, true, {kRoom, kNoDoor, false, false}, 3000) == Verdict::PeerReady, "peer already in");
}

void testReleasesWhenThePeerTurnsAway() {
    check(rule::check(kRoom, true, {kOtherRoom, kNoDoor, false, false}, 100) == Verdict::PeerNotComing,
          "peer's door ended elsewhere");
    check(rule::check(kRoom, true, {kLoading, kOtherRoom, true, false}, 100) == Verdict::PeerNotComing,
          "peer took another door");
}

void testShortTimeoutAndLostPeer() {
    check(rule::check(kRoom, true, inSharedDoor(false), rule::kMaxHoldMs - 1) == Verdict::Hold, "just before the cap");
    check(rule::check(kRoom, true, inSharedDoor(false), rule::kMaxHoldMs) == Verdict::Timeout, "the cap releases");
    check(rule::check(kRoom, false, inSharedDoor(false), 0) == Verdict::NoPeer, "no peer report: release");
}

void testWaitingIsShownOnlyWhenLong() {
    check(!rule::showsWaiting(rule::kWaitingToastMs - 1), "a normal load gap stays invisible");
    check(rule::showsWaiting(rule::kWaitingToastMs), "a long wait is shown");
}

}  // namespace

int main() {
    testHoldsOnlyInTheSameSharedDoor();
    testSimultaneousFinish();
    testReleasesWhenThePeerIsReady();
    testReleasesWhenThePeerTurnsAway();
    testShortTimeoutAndLostPeer();
    testWaitingIsShownOnlyWhenLong();
    if (g_failures == 0) std::printf("room_gate_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
