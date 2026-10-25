// Checks the pose puppet's pure rules (no game): replay flags, frame distance, the seek decision, which anim control a
// request belongs to, the hand pose, the hit attribute diff and the weapon refresh.
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
constexpr uintptr_t kBodyControl = 0xff8;
constexpr uintptr_t kFirstSubControl = 0x67dc;
constexpr uintptr_t kSubStride = 0x20;
constexpr uint8_t kHandgun = 2;
constexpr uint8_t kShotgun = 5;
constexpr int32_t kNoHeldWeapon = -1;
constexpr uint32_t kBegin = 5000;
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
bool seeksAfter(pr::SeekTracker& tracker, const pr::LayerFrame& puppet, const pw::Layer& owner, uint32_t count,
                bool driftAllowed = true) {
    pr::Verdict verdict = pr::Verdict::Follow;
    for (uint32_t i = 0; i < count; ++i) verdict = tracker.update(puppet, owner, driftAllowed);
    return verdict == pr::Verdict::Seek;
}

void testSeek() {
    const pr::LayerFrame puppet{kWalk, 20.0f, kLoopLength};
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

    pr::SeekTracker placed;
    placed.requestSeek();
    check(!seeksAfter(placed, puppet, {pw::kNoMotion, 0.0f}, 1), "a requested seek waits for an owner motion");
    check(seeksAfter(placed, puppet, {kWalk, 20.0f}, 1), "a requested seek happens at once even when the motions agree");
    check(!seeksAfter(placed, puppet, {kWalk, 20.0f}, 1), "and only once");
    placed.requestSeek();
    placed.reset();
    check(!seeksAfter(placed, puppet, {kWalk, 20.0f}, 1), "a reset forgets a requested seek");

    pr::SeekTracker looping;
    const pr::LayerFrame wrapped{kWalk, 59.0f, kLoopLength};
    check(!seeksAfter(looping, wrapped, {kWalk, 2.0f}, 50), "a loop wrap is not drift");

    pr::SeekTracker falling;
    const pw::Layer ahead{kWalk, 20.0f + pr::kSeekFrames + 1.0f};
    check(seeksAfter(falling, puppet, ahead, pr::kSeekHoldTicks), "setup: drift seeks once");
    check(!seeksAfter(falling, puppet, ahead, pr::kDriftSeekCooldownTicks - 1), "drift right after a seek waits out the cooldown");
    check(seeksAfter(falling, puppet, ahead, pr::kSeekHoldTicks), "drift seeks again once the cooldown is over");
    check(seeksAfter(falling, puppet, {kRun, 5.0f}, pr::kSeekHoldTicks), "another motion seeks inside the cooldown");

    pr::SeekTracker dead;
    check(!seeksAfter(dead, puppet, ahead, 200, false), "with drift not allowed (a dead enemy) drift never seeks");
    check(seeksAfter(dead, puppet, {kRun, 5.0f}, pr::kSeekHoldTicks, false), "but another motion still does");

    pr::SeekTracker still;
    const pw::Layer held{kWalk, 0.0f};
    const pr::LayerFrame standingUp{kWalk, 4.0f, kLoopLength};
    pr::Verdict verdict = pr::Verdict::Follow;
    for (uint32_t i = 0; i < pr::kStillOwnerTicks; ++i) verdict = still.update(standingUp, held, true, true);
    check(verdict == pr::Verdict::Follow, "an owner frame still for less than the limit is not held");
    check(still.update(standingUp, held, true, true) == pr::Verdict::Hold, "an owner frame that stays put is held at once");
    check(still.update({kWalk, held.frame, kLoopLength}, held, true, true) == pr::Verdict::Follow, "a puppet on the held frame is left alone");
    pr::SeekTracker notAsked;
    for (uint32_t i = 0; i < 2 * pr::kStillOwnerTicks; ++i) verdict = notAsked.update(standingUp, held);
    check(verdict == pr::Verdict::Follow, "only a caller that asks holds a still owner");
}

const pr::ControlLayout kLayout{kBodyControl, kFirstSubControl, kSubStride};

void testControlTarget() {
    check(pr::controlTarget(kLayout, kBodyControl) == pw::kBodyTarget, "the body control is target 0");
    check(pr::controlTarget(kLayout, kFirstSubControl) == 1, "sub-control 0 is target 1");
    check(pr::controlTarget(kLayout, kFirstSubControl + 3 * kSubStride) == 4, "the weapon sub-control (k = 3) is target 4");
    check(pr::controlTarget(kLayout, kFirstSubControl + 6 * kSubStride) == 7, "the last sub-control is target 7");
    check(!pr::controlTarget(kLayout, kFirstSubControl + 7 * kSubStride), "the eighth sub-control does not exist");
    check(!pr::controlTarget(kLayout, kFirstSubControl + 1), "an offset inside a sub-control is not its control");
    check(!pr::controlTarget(kLayout, kFirstSubControl - 1), "an offset before the first sub-control is none");
    check(!pr::controlTarget(kLayout, 0), "the character itself is not an anim control");
    check(!pr::controlTarget(kLayout, kBodyControl + 4), "an offset next to the body control is none");
    for (uint16_t target = 0; target <= pw::kSubControls; ++target) {
        const auto offset = pr::controlOffset(kLayout, target);
        check(offset && pr::controlTarget(kLayout, *offset) == target, "target to offset to target round trips");
    }
    check(!pr::controlOffset(kLayout, pw::kSubControls + 1), "a target past the sub-controls has no offset");
}

void testHandPose() {
    check(pr::needsHandPose(0, pw::kBodyTarget), "a layer-0 body request is followed by the hand pose");
    check(!pr::needsHandPose(1, pw::kBodyTarget), "other layers are not");
    check(!pr::needsHandPose(0, 4), "a weapon sub-control request is not");
    check(!pr::needsHandPose(3, 4), "neither is its layer 3");
}

void testHitChange() {
    const pr::HitChange none = pr::hitChange(0x100, 0x100);
    check(none.set == 0 && none.clear == 0, "equal attributes change nothing");
    const pr::HitChange both = pr::hitChange(0x101, 0x202);
    check(both.set == 0x202 && both.clear == 0x101, "bits to set and to clear are the differences");
    const pr::HitChange masked = pr::hitChange(0xfc00, 0x1400);
    check(masked.set == 0 && masked.clear == 0, "only the low ten bits count");
    const pr::HitChange fromZero = pr::hitChange(0, 0x3ff);
    check(fromZero.set == 0x3ff && fromZero.clear == 0, "all ten bits can be set");
    const pr::HitChange toZero = pr::hitChange(0x3ff, 0);
    check(toZero.set == 0 && toZero.clear == 0x3ff, "and cleared");
}

void testWeaponRefresh() {
    check(!pr::needsWeaponRefresh(kHandgun, kHandgun, 5000, false), "the same weapon never needs a refresh");
    check(!pr::needsWeaponRefresh(kShotgun, kHandgun, pr::kWeaponRefreshMs, false), "exactly a second is not over a second");
    check(pr::needsWeaponRefresh(kShotgun, kHandgun, pr::kWeaponRefreshMs + 1, false), "over a second asks");
    check(!pr::needsWeaponRefresh(kShotgun, kHandgun, 5000, true), "a pending refresh waits");
    check(pr::needsWeaponRefresh(kHandgun, kNoHeldWeapon, 5000, false), "an empty hand against a weapon asks");

    pr::WeaponWatch watch;
    uint32_t now = kBegin;
    check(!watch.update(kHandgun, kHandgun, now, false), "equal weapons: nothing");
    check(!watch.update(kShotgun, kHandgun, now, false), "the mismatch starts the clock");
    now += pr::kWeaponRefreshMs;
    check(!watch.update(kShotgun, kHandgun, now, false), "a second of it is not enough");
    now += 1;
    check(watch.update(kShotgun, kHandgun, now, false), "over a second asks");
    check(!watch.update(kShotgun, kHandgun, now + 1, false), "and then waits again");
    now += pr::kWeaponRefreshMs + 2;
    check(watch.update(kShotgun, kHandgun, now, false), "a mismatch that stays asks again a second later");
    check(!watch.update(kShotgun, kHandgun, now + pr::kWeaponRefreshMs + 5, true), "a pending refresh clears the clock");
    check(!watch.update(kShotgun, kHandgun, now + pr::kWeaponRefreshMs + 6, false), "which starts over afterwards");
    check(!watch.update(kShotgun, kShotgun, now + 3 * pr::kWeaponRefreshMs, false), "agreeing clears it");

    pr::WeaponWatch wrapping;
    const uint32_t nearWrap = 0xfffffff0u;
    check(!wrapping.update(kShotgun, kHandgun, nearWrap, false), "setup across the 32-bit clock wrap");
    check(wrapping.update(kShotgun, kHandgun, nearWrap + pr::kWeaponRefreshMs + 100, false), "the clock wraps safely");
    wrapping.reset();
    check(!wrapping.update(kShotgun, kHandgun, 7, false), "a reset forgets the mismatch");
}

}  // namespace

int main() {
    testFlags();
    testDistance();
    testSeek();
    testControlTarget();
    testHandPose();
    testHitChange();
    testWeaponRefresh();
    if (g_failures == 0) std::printf("puppet_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
