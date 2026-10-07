// Checks the room-entry barrier rule (no game). Exit 0 when every check passes.
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

void testHoldsOnlyForAPeerOnItsWayHere() {
    check(rule::holdsAtArrival(kRoom, {kLoading, kRoom}), "peer loading into this room: hold");
    check(rule::holdsAtArrival(kRoom, {kOtherRoom, kRoom}), "peer's door into this room just started: hold");
    check(!rule::holdsAtArrival(kRoom, {kRoom, kNoDoor}), "peer already here: no hold");
    check(!rule::holdsAtArrival(kRoom, {kOtherRoom, kNoDoor}), "peer staying in another room: no hold");
    check(!rule::holdsAtArrival(kRoom, {kLoading, kOtherRoom}), "peer travelling elsewhere: no hold");
}

void testReleasesWhenThePeerArrives() {
    check(rule::check(kRoom, true, {kLoading, kRoom}, 3000) == Verdict::Hold, "still loading: hold");
    check(rule::check(kRoom, true, {kRoom, kNoDoor}, 3000) == Verdict::PeerArrived, "peer reports this room");
    check(rule::check(kRoom, true, {kRoom, kRoom}, 3000) == Verdict::PeerArrived,
          "this room loaded on the peer counts even before its door report clears");
}

void testReleasesWhenThePeerTurnsAway() {
    check(rule::check(kRoom, true, {kOtherRoom, kNoDoor}, 100) == Verdict::PeerNotComing, "peer's door ended elsewhere");
    check(rule::check(kRoom, true, {kLoading, kOtherRoom}, 100) == Verdict::PeerNotComing, "peer took another door");
}

void testTimeoutAndLostPeer() {
    check(rule::check(kRoom, true, {kLoading, kRoom}, rule::kMaxHoldMs - 1) == Verdict::Hold, "just before the cap");
    check(rule::check(kRoom, true, {kLoading, kRoom}, rule::kMaxHoldMs) == Verdict::Timeout, "the cap releases");
    check(rule::check(kRoom, false, {kLoading, kRoom}, 0) == Verdict::NoPeer, "no peer report: release");
}

}  // namespace

int main() {
    testHoldsOnlyForAPeerOnItsWayHere();
    testReleasesWhenThePeerArrives();
    testReleasesWhenThePeerTurnsAway();
    testTimeoutAndLostPeer();
    if (g_failures == 0) std::printf("room_gate_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
