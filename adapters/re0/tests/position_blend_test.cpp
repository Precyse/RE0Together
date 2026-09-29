// Checks the remote-position correction math (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/position_blend.h"

namespace {

namespace pb = position_blend;

constexpr float kEpsilon = 1e-4f;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < kEpsilon; }

void testClassify() {
    check(pb::classify(0.0f) == pb::Action::Ignore, "zero drift ignored");
    check(pb::classify(pb::kDeadZone - 0.01f) == pb::Action::Ignore, "below dead zone ignored");
    check(pb::classify(pb::kDeadZone) == pb::Action::Blend, "dead zone edge blends");
    check(pb::classify(pb::kSnapDistance) == pb::Action::Blend, "snap distance edge blends");
    check(pb::classify(pb::kSnapDistance + 0.01f) == pb::Action::Snap, "beyond snap distance snaps");
}

void testPosition() {
    const float from[3] = {0, 0, 0};
    const float to[3] = {40, 0, -20};
    float out[3];
    pb::blendPosition(from, to, out);
    check(near(out[0], 40.0f * pb::kBlendPerTick) && near(out[1], 0.0f) && near(out[2], -20.0f * pb::kBlendPerTick), "blend share of the way");
    float previous = pb::distance(from, to);
    float current[3] = {0, 0, 0};
    for (int i = 0; i < 20; ++i) {
        pb::blendPosition(current, to, out);
        for (int k = 0; k < 3; ++k) current[k] = out[k];
        const float now = pb::distance(current, to);
        check(now < previous, "drift shrinks every tick");
        previous = now;
    }
    check(previous < pb::kDeadZone, "converges into the dead zone");
}

void testExtrapolation() {
    const float pos[3] = {10, 0, 0};
    float velocity[3];
    const float earlier[3] = {7, 0, 0};
    pb::velocity(earlier, pos, 0.5f, velocity);
    check(near(velocity[0], 6.0f), "velocity from two samples");
    float out[3];
    pb::extrapolate(pos, velocity, 0.05f, out);
    check(near(out[0], 10.3f), "extrapolates by elapsed time");
    pb::extrapolate(pos, velocity, 1.0f, out);
    check(near(out[0], 10.0f + 6.0f * pb::kMaxExtrapolationSeconds), "extrapolation capped");
    pb::extrapolate(pos, velocity, -1.0f, out);
    check(near(out[0], 10.0f), "negative elapsed time ignored");
    pb::velocity(earlier, pos, 0.0f, velocity);
    check(near(velocity[0], 0.0f), "zero interval gives zero velocity");
}

void testRotation() {
    const float identity[4] = {0, 0, 0, 1};
    const float quarter[4] = {0, std::sqrt(0.5f), 0, std::sqrt(0.5f)};  // 90 degrees about y
    float out[4];
    pb::blendRotation(identity, quarter, out);
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    check(near(length, 1.0f), "result is normalized");
    check(out[1] > 0.0f && out[1] < quarter[1], "moves toward the target");
    const float negated[4] = {0, -quarter[1], 0, -quarter[3]};  // same rotation, opposite sign
    float shortArc[4];
    pb::blendRotation(identity, negated, shortArc);
    check(near(shortArc[1], out[1]) && near(shortArc[3], out[3]), "takes the shorter arc");
}

}  // namespace

int main() {
    testClassify();
    testPosition();
    testExtrapolation();
    testRotation();
    if (g_failures == 0) std::printf("position_blend_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
