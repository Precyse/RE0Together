// Checks the receiver's pose timeline (no game): clock mapping, interpolation, discrete fields from the older sample,
// motion events handed out once in order, every hard reset reason, staleness and held poses, the shared clock. Exit 0
// when every check passes.
#include <cmath>
#include <cstdio>
#include <vector>

#include "../src/pose_timeline.h"

namespace {

namespace pt = pose_timeline;
namespace pw = pose_wire;
namespace sc = stream_clock;

constexpr float kEpsilon = 1e-3f;
constexpr uint32_t kTransitMs = 40;  // arrival minus sender clock in the tests
constexpr uint32_t kSendGapMs = 33;  // 30 Hz
constexpr uint32_t kHeldGapMs = 250;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < kEpsilon; }

pw::Pose poseAt(uint32_t senderMs, uint32_t seq, float x, uint16_t motion = 0x209, float frame = 0.0f) {
    pw::Pose pose;
    pose.senderMs = senderMs;
    pose.seq = seq;
    pose.pos[0] = x;
    pose.layer[0] = {motion, frame};
    pose.room = 1;
    pose.loadSerial = 1;
    return pose;
}

pw::MotionEvent eventAt(uint32_t senderMs, uint32_t seq, uint16_t motion = 0x209) {
    return {senderMs, 0, 4.0f, 0.0f, seq, motion, 0};
}

// The local clock at which the owner's `senderMs` is shown.
uint32_t shownAt(uint32_t senderMs) { return senderMs + kTransitMs + sc::kMinDelayMs; }

// A timeline with the clock it reads.
struct Rig {
    sc::StreamClock clock;
    pt::Timeline timeline;
    pw::Pose out;
    std::vector<pw::MotionEvent> due;

    void arrive(const pw::Pose& pose) { timeline.addPose(clock, pose, pose.senderMs + kTransitMs); }
    void arrive(const pw::MotionEvent& event) { timeline.addEvent(clock, event, event.senderMs + kTransitMs); }
    pt::Result render(uint32_t nowMs) { return timeline.render(clock, nowMs, out, due); }
};

void testNothingYet() {
    Rig rig;
    check(!rig.render(1000).valid, "an empty timeline shows nothing");
    rig.arrive(poseAt(1000, 1, 0.0f));
    check(!rig.render(shownAt(1000) - 1).valid, "nothing before the first pose's time");
    const pt::Result result = rig.render(shownAt(1000));
    check(result.valid && result.reset == pt::Reset::None, "the first pose shows at its time, without a reset");
}

void testInterpolation() {
    Rig rig;
    pw::Pose a = poseAt(1000, 1, 0.0f, 0x209, 10.0f);
    pw::Pose b = poseAt(1000 + kSendGapMs, 2, 33.0f, 0x209, 20.0f);
    a.manualPitch = 0.0f;
    b.manualPitch = 0.3f;
    rig.arrive(a);
    rig.arrive(b);
    rig.render(shownAt(1000) + 11);
    check(near(rig.out.pos[0], 11.0f), "position is interpolated by owner time");
    check(near(rig.out.layer[0].frame, 13.33333f), "frame is interpolated on the same motion");
    check(near(rig.out.manualPitch, 0.1f), "aim pitch is interpolated");
    check(rig.out.layer[0].motion == 0x209, "the motion stays");
    rig.render(shownAt(1000) + 100);
    check(near(rig.out.pos[0], 33.0f), "after the newest pose it is held, not extrapolated");
}

void testDiscreteFieldsFromOlderSample() {
    Rig rig;
    pw::Pose a = poseAt(1000, 1, 0.0f, 0x209, 40.0f);
    pw::Pose b = poseAt(1000 + kSendGapMs, 2, 10.0f, 0x20a, 3.0f);
    a.state = 1;
    b.state = 2;
    a.weapon = 3;
    b.weapon = 4;
    a.held = false;
    b.held = true;
    rig.arrive(a);
    rig.arrive(b);
    rig.render(shownAt(1000) + 5);
    check(rig.out.layer[0].motion == 0x209 && near(rig.out.layer[0].frame, 40.0f) && rig.out.state == 1, "the older sample before halfway");
    rig.render(shownAt(1000) + 28);
    check(rig.out.layer[0].motion == 0x209 && rig.out.state == 1 && rig.out.weapon == 3 && !rig.out.held,
          "the older sample after halfway too");
    rig.render(shownAt(1000) + kSendGapMs);
    check(rig.out.state == 2 && rig.out.weapon == 4 && rig.out.held, "the newer one once its time is reached");

    Rig wrapping;
    wrapping.arrive(poseAt(1000, 1, 0.0f, 0x209, 58.0f));
    wrapping.arrive(poseAt(1000 + kSendGapMs, 2, 0.0f, 0x209, 2.0f));
    wrapping.render(shownAt(1000) + 5);
    check(near(wrapping.out.layer[0].frame, 58.0f), "a frame that went backwards (a loop) is not interpolated");
}

void testOrderAndDuplicates() {
    Rig rig;
    rig.arrive(poseAt(1000 + 2 * kSendGapMs, 3, 66.0f));
    rig.arrive(poseAt(1000, 1, 0.0f));
    rig.arrive(poseAt(1000 + kSendGapMs, 2, 33.0f));
    rig.arrive(poseAt(1000 + kSendGapMs, 2, 99.0f));  // a duplicate of seq 2
    rig.render(shownAt(1000) + kSendGapMs);
    check(near(rig.out.pos[0], 33.0f), "poses arriving out of order are sorted, a duplicate is ignored");
}

void testEvents() {
    Rig rig;
    rig.arrive(poseAt(1000, 1, 0.0f));
    rig.arrive(eventAt(1010, 2));
    rig.arrive(eventAt(1020, 1));
    rig.arrive(eventAt(1500, 3));
    auto result = rig.render(shownAt(1000) + 5);
    check(result.valid && rig.due.empty(), "no event is handed out before its time");
    rig.render(shownAt(1020));
    check(rig.due.size() == 2 && rig.due[0].seq == 1 && rig.due[1].seq == 2, "events due are handed out in sequence order");
    rig.due.clear();
    rig.render(shownAt(1020) + 1);
    check(rig.due.empty(), "an event is handed out once");
    rig.arrive(eventAt(1010, 2));
    rig.arrive(eventAt(1020, 1));
    rig.render(shownAt(1020) + 2);
    check(rig.due.empty(), "a duplicate of a handed-out event is dropped");
    rig.render(shownAt(1500));
    check(rig.due.size() == 1 && rig.due[0].seq == 3, "a later event comes when its time does");
}

void testStaleEvents() {
    Rig rig;
    rig.arrive(poseAt(1000, 1, 0.0f));
    rig.arrive(eventAt(1010, 1));
    rig.arrive(eventAt(4000, 2));
    rig.arrive(poseAt(1000 + kSendGapMs, 2, 0.0f));
    rig.arrive(poseAt(4000, 3, 0.0f));
    rig.arrive(poseAt(5000, 4, 0.0f));
    const pt::Result result = rig.render(shownAt(5000));
    check(result.staleDropped == 1 && rig.due.size() == 1 && rig.due[0].seq == 2, "an event far behind the render time is dropped, a recent one kept");
}

void testClockTracking() {
    Rig rig;
    rig.timeline.addPose(rig.clock, poseAt(1000, 1, 0.0f), 1000 + 60);
    rig.timeline.addPose(rig.clock, poseAt(1033, 2, 10.0f), 1033 + 40);  // the faster path sets the transit time
    rig.timeline.addPose(rig.clock, poseAt(1066, 3, 20.0f), 1066 + 90);  // a slow packet does not move it
    rig.render(1033 + 40 + rig.clock.delayMs());
    check(near(rig.out.pos[0], 10.0f), "the smallest transit time maps the clocks");
}

void testSenderClockRestart() {
    Rig rig;
    rig.arrive(poseAt(500000, 1, 0.0f));
    rig.arrive(eventAt(500010, 1));
    rig.timeline.addPose(rig.clock, poseAt(100, 1, 7.0f), 800000 + 100 + kTransitMs);  // the peer's game restarted
    const pt::Result result = rig.render(800000 + 100 + kTransitMs + sc::kMinDelayMs);
    check(result.valid && near(rig.out.pos[0], 7.0f) && rig.due.empty(), "a restarted sender clock starts the timeline over");
    check(result.reset == pt::Reset::Clock, "and says why");
    rig.timeline.addEvent(rig.clock, eventAt(110, 1), 800000 + 110 + kTransitMs);
    rig.render(800000 + 110 + kTransitMs + sc::kMinDelayMs);
    check(rig.due.size() == 1, "its numbering starts over too");
}

void testClockWrap() {
    Rig rig;
    constexpr uint32_t kBeforeWrap = 0xffffffffu - 20;
    rig.arrive(poseAt(kBeforeWrap, 1, 0.0f));
    rig.arrive(poseAt(kBeforeWrap + kSendGapMs, 2, 33.0f));  // wraps past zero
    rig.render(shownAt(kBeforeWrap) + 11);
    check(near(rig.out.pos[0], 11.0f), "interpolation works across the 32-bit clock wrap");
}

// A steady stream of poses from 1000 for `count` samples, 33 ms apart, all at x = 0.
void stream(Rig& rig, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) rig.arrive(poseAt(1000 + i * kSendGapMs, i + 1, 0.0f));
}

void testResetOnSerial() {
    Rig rig;
    stream(rig, 3);
    pw::Pose next = poseAt(1000 + 3 * kSendGapMs, 4, 5.0f);
    next.loadSerial = 2;
    rig.arrive(next);
    pt::Result result = rig.render(shownAt(1000) + 1);
    check(result.valid && result.reset == pt::Reset::Serial, "a changed load serial is a hard reset");
    check(near(rig.out.pos[0], 5.0f), "the newest pose is shown at once, before its time");
    result = rig.render(shownAt(1000) + 2);
    check(result.valid && result.reset == pt::Reset::None && near(rig.out.pos[0], 5.0f), "it is held, and the reset is reported once");
    rig.render(shownAt(1000 + 3 * kSendGapMs) + 1);
    check(near(rig.out.pos[0], 5.0f), "the stream carries on from it");
}

void testResetOnRoom() {
    Rig rig;
    stream(rig, 3);
    pw::Pose next = poseAt(1000 + 3 * kSendGapMs, 4, 0.0f);
    next.room = 9;
    rig.arrive(next);
    check(rig.render(shownAt(1000)).reset == pt::Reset::Room, "a changed room with the same load serial is a hard reset");
}

void testResetOnGap() {
    constexpr uint32_t kLastArrival = 1000 + 2 * kSendGapMs + kTransitMs;
    Rig sender;
    stream(sender, 3);
    sender.timeline.addPose(sender.clock, poseAt(1000 + 3 * kSendGapMs + pt::kGapResetMs + 1, 4, 0.0f), kLastArrival + kSendGapMs);
    check(sender.render(kLastArrival + kSendGapMs).reset == pt::Reset::Gap, "a sender clock gap over 500 ms is a hard reset");

    Rig arrival;
    stream(arrival, 3);
    const uint32_t lateMs = kLastArrival + pt::kGapResetMs + 1;
    arrival.timeline.addPose(arrival.clock, poseAt(1000 + 3 * kSendGapMs, 4, 0.0f), lateMs);
    check(arrival.render(lateMs).reset == pt::Reset::Gap, "an arrival gap over 500 ms is a hard reset");

    Rig exact;
    stream(exact, 3);
    exact.timeline.addPose(exact.clock, poseAt(1000 + 3 * kSendGapMs, 4, 0.0f), kLastArrival + pt::kGapResetMs);
    check(exact.render(kLastArrival + pt::kGapResetMs).reset == pt::Reset::None, "a gap of exactly 500 ms is not one");

    Rig held;
    stream(held, 1);
    uint32_t seq = 2;
    for (uint32_t senderMs = 1000 + kHeldGapMs; senderMs <= 6000; senderMs += kHeldGapMs) {
        pw::Pose pose = poseAt(senderMs, seq++, 0.0f);
        pose.held = true;
        held.arrive(pose);
    }
    check(held.render(shownAt(6000)).reset == pt::Reset::None, "held poses at 4 Hz never make a gap");
}

void testResetOnTeleport() {
    Rig near_;
    stream(near_, 3);
    near_.arrive(poseAt(1000 + 3 * kSendGapMs, 4, pt::kTeleportUnits - 1.0f));
    check(near_.render(shownAt(1000)).reset == pt::Reset::None, "a jump under kTeleportUnits is a move");

    Rig far_;
    stream(far_, 3);
    far_.arrive(poseAt(1000 + 3 * kSendGapMs, 4, pt::kTeleportUnits));
    const pt::Result result = far_.render(shownAt(1000));
    check(result.reset == pt::Reset::Teleport && near(far_.out.pos[0], pt::kTeleportUnits), "a jump of kTeleportUnits is a hard reset and places the pose");
}

void testResetOnLead() {
    Rig rig;
    stream(rig, 3);
    constexpr uint32_t kLastArrival = 1000 + 2 * kSendGapMs + kTransitMs;
    constexpr uint32_t kLaterTransit = 200;
    constexpr uint32_t kRelaxingSamples = 6000;  // other streams' slow packets relax the tracked transit time upward
    for (uint32_t i = 0; i < kRelaxingSamples; ++i) rig.clock.observe(100000 + i, 100000 + i + kLaterTransit);
    const pt::Result result = rig.render(kLastArrival);
    check(result.valid && result.reset == pt::Reset::Lead, "a newest pose over the delay plus kMaxLeadPastDelayMs ahead of the shown time re-anchors");
    check(near(rig.out.pos[0], 0.0f) && result.buffered == 1, "only the newest pose is kept");
    check(rig.render(kLastArrival + 1).reset == pt::Reset::None, "and the reset is reported once");
}

void testResetOnDepth() {
    Rig rig;
    constexpr uint32_t kBurst = 20;
    constexpr uint32_t kBurstGapMs = 10;
    for (uint32_t i = 0; i < kBurst; ++i) rig.arrive(poseAt(1000 + i * kBurstGapMs, i + 1, 0.0f));
    const pt::Result result = rig.render(1000 + kTransitMs + rig.clock.delayMs());
    check(result.valid && result.reset == pt::Reset::Depth, "more poses ahead of the shown time than the delay holds re-anchors");
    check(result.buffered == 1, "only the newest pose is kept");
}

void testStaleAndHeld() {
    Rig rig;
    stream(rig, 3);
    const uint32_t lastArrival = 1000 + 2 * kSendGapMs + kTransitMs;
    check(!rig.render(lastArrival + pt::kStaleMs).stale, "not stale at kStaleMs");
    pt::Result result = rig.render(lastArrival + pt::kStaleMs + 1);
    check(result.stale && !result.valid, "stale after kStaleMs without a pose: nothing is shown");

    uint32_t seq = 10;
    uint32_t senderMs = 5000;
    for (; senderMs <= 12000; senderMs += kHeldGapMs) {
        pw::Pose pose = poseAt(senderMs, seq++, 0.0f);
        pose.held = true;
        rig.arrive(pose);
        check(!rig.render(senderMs + kTransitMs).stale, "held poses keep the stream fresh");
    }

    result = rig.render(12000 + kTransitMs + pt::kStaleMs + 1);
    check(result.stale, "and stale again when they stop");
    rig.arrive(poseAt(20000, seq++, 3.0f));
    result = rig.render(20000 + kTransitMs);
    check(!result.stale && result.valid && result.reset == pt::Reset::Gap && near(rig.out.pos[0], 3.0f), "a pose after the silence is fresh and placed");
}

void testSharedClock() {
    Rig rig;
    stream(rig, 3);
    rig.clock.reanchor(500);  // another stream on this peer's clock restarted it
    check(!rig.render(shownAt(1000)).valid, "a re-anchored shared clock drops what was buffered on the old mapping");
    rig.timeline.addPose(rig.clock, poseAt(2000, 10, 4.0f), 2000 + 500);
    check(rig.render(2000 + 500).reset == pt::Reset::Clock, "and the next render says so");

    sc::StreamClock used;
    used.observe(1000, 1040);
    used.reanchor(40);
    pt::Timeline late;
    late.addPose(used, poseAt(2000, 1, 0.0f), 2040);
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    check(late.render(used, 2040 + sc::kMinDelayMs, out, due).reset == pt::Reset::None, "a timeline joining a clock that has history is not reset");
}

void testClear() {
    Rig rig;
    stream(rig, 3);
    rig.timeline.clear();
    check(!rig.render(shownAt(1000)).valid, "a cleared timeline shows nothing");
    rig.arrive(poseAt(1000, 1, 2.0f));
    check(rig.render(shownAt(1000)).valid, "and takes a stream again from sequence 1");
}

void testBehind() {
    Rig rig;
    stream(rig, 4);
    const pt::Result result = rig.render(shownAt(1000) + kSendGapMs);
    check(result.behindMs == static_cast<int32_t>(2 * kSendGapMs) && result.buffered >= 3, "behindMs is the newest pose's lead over the shown time");
}

}  // namespace

int main() {
    testNothingYet();
    testInterpolation();
    testDiscreteFieldsFromOlderSample();
    testOrderAndDuplicates();
    testEvents();
    testStaleEvents();
    testClockTracking();
    testSenderClockRestart();
    testClockWrap();
    testResetOnSerial();
    testResetOnRoom();
    testResetOnGap();
    testResetOnTeleport();
    testResetOnLead();
    testResetOnDepth();
    testStaleAndHeld();
    testSharedClock();
    testClear();
    testBehind();
    if (g_failures == 0) std::printf("pose_timeline_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
