// Checks the enemy puppet rules (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/enemy_puppet_rule.h"

namespace {

namespace rule = enemy_puppet_rule;

constexpr float kEpsilon = 1e-3f;
constexpr int64_t kSnapshotGapMs = 50;
constexpr float kExpectedVelocity = 200.0f;  // 10 units in 50 ms
constexpr int32_t kHp = 94;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < kEpsilon; }

const float kIdentity[4] = {0, 0, 0, 1};

void observeAt(rule::Track& track, float x, int64_t nowMs) {
    const float pos[3] = {x, 0, 0};
    rule::observe(track, pos, kIdentity, kHp, nowMs);
}

void testObserveAndAim() {
    rule::Track track;
    observeAt(track, 0, 0);
    check(track.valid && near(track.velocity[0], 0.0f), "first snapshot has no velocity");
    observeAt(track, 10, kSnapshotGapMs);
    check(near(track.velocity[0], kExpectedVelocity), "velocity from two snapshots 50 ms apart");
    float aim[3];
    rule::aim(track, kSnapshotGapMs + 25, aim);
    check(near(aim[0], 15.0f), "aim advances by velocity");
    rule::aim(track, kSnapshotGapMs + 5000, aim);
    check(near(aim[0], 10.0f + kExpectedVelocity * position_blend::kMaxExtrapolationSeconds),
          "aim extrapolation is capped");
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
    testSteps();
    if (g_failures == 0) std::printf("enemy_puppet_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
