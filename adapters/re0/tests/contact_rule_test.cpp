// Checks the contact router's pure rules (no game): the reaction-state classification, the routing truth table, the
// grantable record, the end of a lease on the holder, the authority's lapse and the holder pose bounds. Exit 0 when
// every check passes.
#include <cstdint>
#include <cstdio>

#include "../src/contact_rule.h"

namespace {

namespace rule = contact_rule;
using enemy_follow_rule::Action;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testClassification() {
    for (int32_t state = 0; state <= 5; ++state) check(rule::classify(state) == rule::Kind::Plain, "states 0..5 are plain");
    for (int32_t state = 6; state <= 16; ++state) check(rule::classify(state) == rule::Kind::Paired, "states 6..16 are paired");
    for (int32_t state = 17; state <= 19; ++state) {
        check(rule::classify(state) == rule::Kind::Plain, "death states are plain");
        check(rule::isDeathState(state), "17..19 are death states");
    }
    check(rule::classify(11) == rule::Kind::Paired, "the unnamed state 11 is treated as paired");
    check(rule::classify(-1) == rule::Kind::Plain && rule::classify(20) == rule::Kind::Plain, "states outside the table are plain");
    check(!rule::isDeathState(16) && !rule::isDeathState(20), "death states stop at 19");
    check(rule::held(2, 6) && rule::held(2, 14) && !rule::held(2, 5) && !rule::held(2, 17), "held = group 2 and a paired state");
    check(!rule::held(0, 6) && !rule::held(1, 9), "other think groups never hold");
}

void testRoute() {
    rule::Situation plain{rule::Kind::Plain, true, false, false};
    check(rule::route(plain) == rule::Route::Hit, "a plain contact is a hit");
    plain.slotBusy = true;
    plain.sameVictim = true;
    check(rule::route(plain) == rule::Route::Hit, "a plain contact is a hit even on a leased enemy");

    rule::Situation paired{rule::Kind::Paired, true, false, false};
    check(rule::route(paired) == rule::Route::Lease, "an eligible paired contact leases");
    paired.eligible = false;
    check(rule::route(paired) == rule::Route::Drop, "an ineligible paired contact sends nothing");
    paired.eligible = true;
    paired.slotBusy = true;
    paired.sameVictim = true;
    check(rule::route(paired) == rule::Route::Drop, "a repeat on the lease's victim is dropped");
    paired.sameVictim = false;
    check(rule::route(paired) == rule::Route::Hit, "a second victim of a leased enemy gets a hit only");
}

void testEligibility() {
    check(rule::leaseEligible(10, true), "a living enemy and a victim in the room");
    check(!rule::leaseEligible(0, true), "a dead enemy never leases");
    check(!rule::leaseEligible(-1, true), "a corpse never leases");
    check(!rule::leaseEligible(10, false), "a victim outside the room never leases");
    check(rule::grantable(10, Action{{1, 0xe, 0, 1}}), "an enemy in an action can be granted");
    check(!rule::grantable(10, Action{{2, 0, 0, 0}}), "an enemy in its think state cannot");
    check(!rule::grantable(10, Action{{0, 0, 0, 0}}), "an enemy in setup cannot");
    check(!rule::grantable(0, Action{{1, 0xe, 0, 1}}), "a dead enemy cannot");
}

void testFinished() {
    const Action granted{{1, 0xe, 0, 1}};
    const rule::VictimView holding{2, 6, 100};
    const rule::VictimView free{0, 0, 100};
    check(rule::finished(holding, granted, granted, 50, 1000) == rule::End::None, "same action, victim held: running");
    check(rule::finished(holding, granted, granted, 0, 0) == rule::End::None, "a dead enemy with the victim still held keeps the lease");
    check(rule::finished(holding, granted, granted, 0, 0, rule::kDeadReleaseMs - 1) == rule::End::None, "until the release cap");
    check(rule::finished(holding, granted, granted, 0, 0, rule::kDeadReleaseMs) == rule::End::EnemyDead, "then it ends anyway");
    check(rule::finished({2, 5, 100}, granted, granted, 0, 0) == rule::End::EnemyDead, "a dead enemy whose victim is free ends it");
    check(rule::finished({2, 19, 0}, granted, granted, 50, 0) == rule::End::VictimDied, "a death state ends it at once");
    check(rule::finished({0, 0, 0}, granted, granted, 50, 1000) == rule::End::VictimDied, "a victim without HP ends it");
    const Action moved{{1, 0x10, 0, 1}};
    check(rule::finished(free, moved, granted, 50, 1000) == rule::End::Released, "action left and victim free: released");
    check(rule::finished(holding, moved, granted, 50, 1000) == rule::End::None, "action left but the victim still held: running");
    check(rule::finished(free, granted, granted, 50, 1000) == rule::End::None, "victim free but the enemy still in the action: running");
    check(rule::finished(free, moved, granted, 50, rule::kSettleMs - 1) == rule::End::None, "released waits for the settle time");
    check(rule::finished(free, moved, granted, 50, rule::kSettleMs) == rule::End::Released, "released counts after the settle time");
    check(rule::finished({2, 5, 100}, moved, granted, 50, 1000) == rule::End::Released, "knock-down after the hold is free");
    check(rule::finished(holding, granted, granted, 50, rule::kStuckGrabMs - 1) == rule::End::None, "a long hold runs until the stuck limit");
    check(rule::finished(holding, granted, granted, 50, rule::kStuckGrabMs) == rule::End::Stuck, "then it is stuck");
    check(rule::carriesState(rule::End::Stuck), "a stuck hold carries the holder's state back");
}

void testHitReach() {
    const float victim[3] = {100.0f, 0.0f, 100.0f};
    const float close[3] = {100.0f, 120.0f, 100.0f + rule::kMaxHitReachUnits - 200.0f};
    const float far[3] = {100.0f + rule::kMaxHitReachUnits + 1.0f, 0.0f, 100.0f};
    check(rule::hitReaches(close, victim), "a hit landing on the victim reaches it");
    check(!rule::hitReaches(far, victim), "a hit landing past the reach is refused");
}

void testCarriesState() {
    check(rule::carriesState(rule::End::Released) && rule::carriesState(rule::End::EnemyDead) && rule::carriesState(rule::End::VictimDied),
          "the holder's final state counts when the hold ran");
    check(!rule::carriesState(rule::End::Declined) && !rule::carriesState(rule::End::Expired) && !rule::carriesState(rule::End::Silent) &&
              !rule::carriesState(rule::End::Left) && !rule::carriesState(rule::End::None),
          "it does not when the grant never ran or the holder is gone");
}

void testLapse() {
    check(rule::awayLapse(false, 1500, 0) == rule::End::None, "no sample yet, within the expiry");
    check(rule::awayLapse(false, 1501, 0) == rule::End::Expired, "no sample past the expiry");
    check(rule::awayLapse(true, 9000, 2500) == rule::End::None, "streaming, within the silence limit");
    check(rule::awayLapse(true, 9000, 2501) == rule::End::Silent, "streaming then silent");
    check(!rule::pendingLapsed(1500) && rule::pendingLapsed(1501), "a pending grant lapses past the expiry");
}

void testHolderPose() {
    const float origin[3] = {0, 0, 0};
    const float close[3] = {900, 0, 0};
    const float beyond[3] = {1100, 0, 0};
    check(rule::acceptHolderPose(origin, close, 0, 50, 50), "within the base displacement");
    check(!rule::acceptHolderPose(origin, beyond, 0, 50, 50), "past the base displacement at once");
    check(rule::acceptHolderPose(origin, beyond, 100, 50, 50), "the allowance grows with time");
    const float distant[3] = {40000, 0, 0};
    check(!rule::acceptHolderPose(origin, distant, 60000, 50, 50), "the elapsed time is capped at 2.5 s");
    const float capped[3] = {30500, 0, 0};
    check(rule::acceptHolderPose(origin, capped, 60000, 50, 50), "1000 + 12000 * 2.5 is reachable");
    check(!rule::acceptHolderPose(origin, origin, 100, 51, 50), "an HP above the lease's is refused");
    check(rule::acceptHolderPose(origin, origin, 100, 10, 50), "a lower HP is fine");
}

}  // namespace

int main() {
    testClassification();
    testRoute();
    testEligibility();
    testFinished();
    testCarriesState();
    testLapse();
    testHolderPose();
    testHitReach();
    if (g_failures == 0) std::printf("contact_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
