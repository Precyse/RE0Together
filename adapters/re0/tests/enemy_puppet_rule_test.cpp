// Checks the enemy puppet rules (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/enemy_puppet_rule.h"

namespace {

namespace rule = enemy_puppet_rule;

constexpr float kEpsilon = 1e-3f;
constexpr int64_t kSnapshotGapMs = 50;
constexpr float kExpectedVelocity = 200.0f;  // 10 units in 50 ms
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < kEpsilon; }

rule::Snapshot snapshot(float x, int32_t hp, uint16_t motion = 0, float frame = 0.0f) {
    return {{x, 0, 0}, {0, 0, 0, 1}, hp, motion, frame};
}

void testObserveAndAim() {
    rule::Track track;
    rule::observe(track, snapshot(0, 94), 0);
    check(near(track.velocity[0], 0.0f), "first snapshot has no velocity");
    rule::observe(track, snapshot(10, 94), kSnapshotGapMs);
    check(near(track.velocity[0], kExpectedVelocity), "velocity from two snapshots 50 ms apart");
    float aim[3];
    rule::aim(track, kSnapshotGapMs + 25, aim);
    check(near(aim[0], 15.0f), "aim advances by velocity");
    rule::aim(track, kSnapshotGapMs + 5000, aim);
    check(near(aim[0], 10.0f + kExpectedVelocity * position_blend::kMaxExtrapolationSeconds),
          "aim extrapolation is capped");
}

void testSkipsUpdate() {
    rule::Track track;
    check(!rule::skipsUpdate(track, 0), "no snapshot: the update runs");
    rule::observe(track, snapshot(0, 94), 0);
    check(rule::skipsUpdate(track, 100), "alive and fresh: the update is skipped");
    check(!rule::skipsUpdate(track, rule::kStateFreshMs + 1), "stale snapshot: the update runs");
    rule::observe(track, snapshot(0, 0), 100);
    check(!rule::skipsUpdate(track, 150), "dead: the update runs (death plays out)");
}

void testHitReaction() {
    rule::Track track;
    rule::observe(track, snapshot(0, 94), 0);
    rule::observe(track, snapshot(0, 78), 100);
    check(!rule::skipsUpdate(track, 100 + rule::kReactionMs), "update runs for the reaction window after a hit");
    rule::observe(track, snapshot(0, 78), 100 + rule::kReactionMs + 1);
    check(rule::skipsUpdate(track, 100 + rule::kReactionMs + 50), "update is skipped again after the window");
}

void testMotion() {
    rule::Track track;
    rule::observe(track, snapshot(0, 94, 7, 10.0f), 0);
    check(track.motion == 7 && near(track.frame, 10.0f), "snapshot keeps the motion number and frame");
    check(near(rule::aimFrame(track, 50), 11.5f), "frame advances at playback speed");
    check(near(rule::aimFrame(track, 5000),
               10.0f + rule::kMotionFramesPerSecond * rule::kMaxFrameExtrapolationSeconds),
          "frame extrapolation is capped");
    using Step = rule::MotionStep;
    check(rule::motionStepFor(3, 0.0f, 7, 0.0f) == Step::SetMotion, "another motion is switched");
    check(rule::motionStepFor(7, 10.0f, 7, 10.0f + rule::kFrameTolerance) == Step::Keep, "close frame is kept");
    check(rule::motionStepFor(7, 10.0f, 7, 10.0f + rule::kFrameTolerance + 1.0f) == Step::SetFrame,
          "drifted frame is re-timed");
    check(rule::motionStepFor(7, 30.0f, 7, 10.0f) == Step::SetFrame, "a puppet ahead of the owner is re-timed too");
}

void testSteps() {
    check(rule::stepFor(0.0f) == rule::Step::Hold, "tiny drift held");
    check(rule::stepFor(100.0f) == rule::Step::Blend, "lag is blended, not snapped");
    check(rule::stepFor(rule::kSnapDistance) == rule::Step::Blend, "snap distance still blends");
    check(rule::stepFor(rule::kSnapDistance + 1.0f) == rule::Step::Snap, "a teleport snaps");
}

}  // namespace

int main() {
    testObserveAndAim();
    testSkipsUpdate();
    testHitReaction();
    testMotion();
    testSteps();
    if (g_failures == 0) std::printf("enemy_puppet_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
