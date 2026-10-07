// Checks the follower rules: error correction and the HP backstop (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/enemy_follow_rule.h"

namespace {

namespace rule = enemy_follow_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

void testSmallErrorIsLeftAlone() {
    rule::Correction correction;
    const float local[3] = {100.0f, 0.0f, 100.0f};
    const float owner[3] = {105.0f, 0.0f, 100.0f};
    check(rule::start(correction, local, owner) == rule::Start::None, "inside the dead zone: nothing");
    float delta[3];
    rule::step(correction, delta);
    check(delta[0] == 0.0f && delta[1] == 0.0f && delta[2] == 0.0f, "no movement added");
}

void testErrorIsRemovedOverTicks() {
    rule::Correction correction;
    const float local[3] = {0.0f, 0.0f, 0.0f};
    const float owner[3] = {100.0f, 0.0f, 0.0f};
    check(rule::start(correction, local, owner) == rule::Start::Correct, "drift: corrected");
    float total = 0.0f;
    float delta[3];
    rule::step(correction, delta);
    check(near(delta[0], 100.0f * rule::kSharePerTick), "a share of the error per tick");
    total += delta[0];
    for (int tick = 0; tick < 100; ++tick) {
        rule::step(correction, delta);
        total += delta[0];
    }
    check(near(total, 100.0f), "the whole error is removed, no more");
}

void testNewSnapshotReplacesTheError() {
    rule::Correction correction;
    const float local[3] = {0.0f, 0.0f, 0.0f};
    const float far[3] = {100.0f, 0.0f, 0.0f};
    rule::start(correction, local, far);
    const float closer[3] = {5.0f, 0.0f, 0.0f};
    rule::start(correction, local, closer);
    float delta[3];
    rule::step(correction, delta);
    check(delta[0] == 0.0f, "the newest measurement wins, not a sum");
}

void testJumpSnaps() {
    rule::Correction correction;
    const float local[3] = {0.0f, 0.0f, 0.0f};
    const float owner[3] = {0.0f, 0.0f, rule::kSnapDistance + 1.0f};
    check(rule::start(correction, local, owner) == rule::Start::Snap, "beyond the snap distance: snap");
    float delta[3];
    rule::step(correction, delta);
    check(delta[2] == 0.0f, "a snap leaves no correction behind");
}

void testHpFollowsTheOwner() {
    rule::HpBackstop hp;
    check(hp.applies(94, 78, 1000), "owner lower: applied");
    check(!hp.applies(78, 78, 1100), "equal: nothing");
}

void testHpWaitsAfterAReplayedHit() {
    rule::HpBackstop hp;
    hp.onHitReplayed(1000);
    check(!hp.applies(69, 85, 1000 + rule::kHitSettleMs - 1), "a snapshot older than the hit cannot undo it");
    check(hp.applies(69, 85, 1000 + rule::kHitSettleMs), "after the settle time the owner's value counts");
}

void testDeathComesFromTheHitFirst() {
    rule::HpBackstop hp;
    check(!hp.applies(16, -1, 1000), "an owner death waits for the hit's replay");
    check(!hp.applies(16, -1, 1000 + rule::kDeathFallbackMs - 1), "still waiting");
    check(hp.applies(16, -1, 1000 + rule::kDeathFallbackMs), "no hit explained it: applied");
}

void testOwnerAliveAgainRestartsTheDeathWait() {
    rule::HpBackstop hp;
    hp.applies(16, -1, 1000);
    hp.applies(16, 16, 1500);
    check(!hp.applies(16, -1, 1000 + rule::kDeathFallbackMs), "the wait counts from the latest death report");
}

void testLocalDeadIsNeverRevived() {
    rule::HpBackstop hp;
    check(!hp.applies(-1, 40, 5000), "a dead local enemy keeps its death");
}

}  // namespace

int main() {
    testSmallErrorIsLeftAlone();
    testErrorIsRemovedOverTicks();
    testNewSnapshotReplacesTheError();
    testJumpSnaps();
    testHpFollowsTheOwner();
    testHpWaitsAfterAReplayedHit();
    testDeathComesFromTheHitFirst();
    testOwnerAliveAgainRestartsTheDeathWait();
    testLocalDeadIsNeverRevived();
    if (g_failures == 0) std::printf("enemy_follow_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
