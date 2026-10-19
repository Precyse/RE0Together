// Checks the pose puppet's pure rules (no game): replay flags, frame distance and the seek decision.
// Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/puppet_rule.h"

namespace {

namespace pr = puppet_rule;
namespace pw = pose_wire;

constexpr float kLoopLength = 60.0f;
constexpr float kEpsilon = 1e-4f;
constexpr uint16_t kWalk = 0x209;
constexpr uint16_t kRun = 0x20a;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testFlags() {
    check(pr::replayFlags(0, 0x1f) == 0x0f, "layer 0 loses the keep-root-motion bit");
    check(pr::replayFlags(0, 0x05) == 0x05, "layer 0 keeps its other bits");
    check(pr::replayFlags(1, 0x1f) == 0x1f, "other layers keep every bit");
}

void testDistance() {
    check(std::fabs(pr::frameDistance(10.0f, 14.0f, kLoopLength) - 4.0f) < kEpsilon, "plain distance");
    check(std::fabs(pr::frameDistance(58.0f, 2.0f, kLoopLength) - 4.0f) < kEpsilon, "a loop wraps at its last frame");
    check(std::fabs(pr::frameDistance(58.0f, 2.0f, 0.0f) - 56.0f) < kEpsilon, "no known length: no wrap");
    check(pr::frameDistance(60.0f, 0.0f, kLoopLength) == 0.0f, "the last frame equals the first of a loop");
}

// Feeds the same state for `count` moves; true when a seek came out of the last one.
bool seeksAfter(pr::SeekTracker& tracker, const pr::Layer0& puppet, const pw::Layer& owner, uint32_t count) {
    pr::Verdict verdict = pr::Verdict::Follow;
    for (uint32_t i = 0; i < count; ++i) verdict = tracker.update(puppet, owner);
    return verdict == pr::Verdict::Seek;
}

void testSeek() {
    const pr::Layer0 puppet{kWalk, 20.0f, kLoopLength};
    pr::SeekTracker tracker;
    check(!seeksAfter(tracker, puppet, {kWalk, 22.0f}, 50), "close frames on the same motion never seek");
    check(!seeksAfter(tracker, puppet, {pw::kNoMotion, 0.0f}, 50), "an owner without a motion never seeks");
    check(!seeksAfter(tracker, puppet, {kRun, 5.0f}, pr::kSeekHoldTicks - 1), "a different motion waits out the hold");
    check(seeksAfter(tracker, puppet, {kRun, 5.0f}, 1), "a different motion seeks after the hold");
    check(!seeksAfter(tracker, puppet, {kRun, 5.0f}, 1), "the hold starts over after a seek");
    tracker.reset();
    check(seeksAfter(tracker, puppet, {kWalk, 20.0f + pr::kSeekFrames + 1.0f}, pr::kSeekHoldTicks), "frames too far apart seek");
    check(!seeksAfter(tracker, puppet, {kWalk, 20.0f + pr::kSeekFrames - 1.0f}, 50), "frames within the limit do not");

    pr::SeekTracker interrupted;
    check(!seeksAfter(interrupted, puppet, {kRun, 5.0f}, pr::kSeekHoldTicks - 1), "setup: almost a seek");
    check(!seeksAfter(interrupted, puppet, {kWalk, 20.0f}, 1), "agreeing again clears the hold");
    check(!seeksAfter(interrupted, puppet, {kRun, 5.0f}, pr::kSeekHoldTicks - 1), "the hold counts from zero");

    pr::SeekTracker looping;
    const pr::Layer0 wrapped{kWalk, 59.0f, kLoopLength};
    check(!seeksAfter(looping, wrapped, {kWalk, 2.0f}, 50), "a loop wrap is not drift");
}

}  // namespace

int main() {
    testFlags();
    testDistance();
    testSeek();
    if (g_failures == 0) std::printf("puppet_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
