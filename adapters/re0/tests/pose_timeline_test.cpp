// Checks the receiver's pose timeline (no game): clock mapping, interpolation, motion events handed out once in
// order, stale and duplicate events, a restarted sender clock. Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <vector>

#include "../src/pose_timeline.h"

namespace {

namespace pt = pose_timeline;
namespace pw = pose_wire;

constexpr float kEpsilon = 1e-3f;
constexpr uint32_t kTransitMs = 40;   // arrival minus sender clock in the tests
constexpr uint32_t kSendGapMs = 33;   // 30 Hz
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
    return pose;
}

pw::MotionEvent eventAt(uint32_t senderMs, uint32_t seq, uint16_t motion = 0x209) {
    return {senderMs, 0, 4.0f, 0.0f, seq, motion, 0};
}

// The local clock at which the owner's `senderMs` is shown.
uint32_t shownAt(uint32_t senderMs) { return senderMs + kTransitMs + pt::kRenderDelayMs; }

void arrive(pt::Timeline& timeline, const pw::Pose& pose) { timeline.addPose(pose, pose.senderMs + kTransitMs); }
void arrive(pt::Timeline& timeline, const pw::MotionEvent& event) { timeline.addEvent(event, event.senderMs + kTransitMs); }

void testNothingYet() {
    pt::Timeline timeline;
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    check(!timeline.render(1000, out, due).valid, "an empty timeline shows nothing");
    arrive(timeline, poseAt(1000, 1, 0.0f));
    check(!timeline.render(shownAt(1000) - 1, out, due).valid, "nothing before the first pose's time");
    check(timeline.render(shownAt(1000), out, due).valid, "the first pose shows at its time");
}

void testInterpolation() {
    pt::Timeline timeline;
    arrive(timeline, poseAt(1000, 1, 0.0f, 0x209, 10.0f));
    arrive(timeline, poseAt(1000 + kSendGapMs, 2, 33.0f, 0x209, 20.0f));
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    timeline.render(shownAt(1000) + 11, out, due);
    check(near(out.pos[0], 11.0f), "position is interpolated by owner time");
    check(near(out.layer[0].frame, 13.33333f), "frame is interpolated on the same motion");
    check(out.layer[0].motion == 0x209, "the motion stays");
    timeline.render(shownAt(1000) + 100, out, due);
    check(near(out.pos[0], 33.0f), "after the newest pose it is held");
}

void testNearestForDiscreteFields() {
    pt::Timeline timeline;
    pw::Pose a = poseAt(1000, 1, 0.0f, 0x209, 40.0f);
    pw::Pose b = poseAt(1000 + kSendGapMs, 2, 10.0f, 0x20a, 3.0f);
    a.state = 1;
    b.state = 2;
    arrive(timeline, a);
    arrive(timeline, b);
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    timeline.render(shownAt(1000) + 5, out, due);
    check(out.layer[0].motion == 0x209 && near(out.layer[0].frame, 40.0f) && out.state == 1, "the nearer sample before halfway");
    timeline.render(shownAt(1000) + 28, out, due);
    check(out.layer[0].motion == 0x20a && near(out.layer[0].frame, 3.0f) && out.state == 2, "the nearer sample after halfway");

    pt::Timeline wrapping;
    arrive(wrapping, poseAt(1000, 1, 0.0f, 0x209, 58.0f));
    arrive(wrapping, poseAt(1000 + kSendGapMs, 2, 0.0f, 0x209, 2.0f));
    wrapping.render(shownAt(1000) + 5, out, due);
    check(near(out.layer[0].frame, 58.0f), "a frame that went backwards (a loop) is not interpolated");
}

void testOrderAndDuplicates() {
    pt::Timeline timeline;
    arrive(timeline, poseAt(1000 + 2 * kSendGapMs, 3, 66.0f));
    arrive(timeline, poseAt(1000, 1, 0.0f));
    arrive(timeline, poseAt(1000 + kSendGapMs, 2, 33.0f));
    arrive(timeline, poseAt(1000 + kSendGapMs, 2, 99.0f));  // a duplicate of seq 2
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    timeline.render(shownAt(1000) + kSendGapMs, out, due);
    check(near(out.pos[0], 33.0f), "poses arriving out of order are sorted, a duplicate is ignored");
}

void testEvents() {
    pt::Timeline timeline;
    arrive(timeline, poseAt(1000, 1, 0.0f));
    arrive(timeline, eventAt(1010, 2));
    arrive(timeline, eventAt(1020, 1));
    arrive(timeline, eventAt(1500, 3));
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    auto result = timeline.render(shownAt(1000) + 5, out, due);
    check(result.valid && due.empty(), "no event is handed out before its time");
    due.clear();
    timeline.render(shownAt(1020), out, due);
    check(due.size() == 2 && due[0].seq == 1 && due[1].seq == 2, "events due are handed out in sequence order");
    due.clear();
    timeline.render(shownAt(1020) + 1, out, due);
    check(due.empty(), "an event is handed out once");
    arrive(timeline, eventAt(1010, 2));
    arrive(timeline, eventAt(1020, 1));
    timeline.render(shownAt(1020) + 2, out, due);
    check(due.empty(), "a duplicate of a handed-out event is dropped");
    timeline.render(shownAt(1500), out, due);
    check(due.size() == 1 && due[0].seq == 3, "a later event comes when its time does");
}

void testStaleEvents() {
    pt::Timeline timeline;
    arrive(timeline, poseAt(1000, 1, 0.0f));
    arrive(timeline, eventAt(1010, 1));
    arrive(timeline, eventAt(4000, 2));
    arrive(timeline, poseAt(5000, 2, 0.0f));
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    const auto result = timeline.render(shownAt(5000), out, due);
    check(result.staleDropped == 1 && due.size() == 1 && due[0].seq == 2, "an event far behind the render time is dropped, a recent one kept");
}

void testClockTracking() {
    pt::Timeline timeline;
    timeline.addPose(poseAt(1000, 1, 0.0f), 1000 + 60);
    timeline.addPose(poseAt(1033, 2, 10.0f), 1033 + 40);  // the faster path sets the transit time
    timeline.addPose(poseAt(1066, 3, 20.0f), 1066 + 90);  // a slow packet does not move it
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    timeline.render(shownAt(1033), out, due);
    check(near(out.pos[0], 10.0f), "the smallest transit time maps the clocks");
}

void testSenderClockRestart() {
    pt::Timeline timeline;
    arrive(timeline, poseAt(500000, 1, 0.0f));
    arrive(timeline, eventAt(500010, 1));
    timeline.addPose(poseAt(100, 1, 7.0f), 800000 + 100 + kTransitMs);  // the peer's game restarted: its clock is far off
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    const auto result = timeline.render(800000 + 100 + kTransitMs + pt::kRenderDelayMs, out, due);
    check(result.valid && near(out.pos[0], 7.0f) && due.empty(), "a restarted sender clock starts the timeline over");
    timeline.addEvent(eventAt(110, 1), 800000 + 110 + kTransitMs);
    timeline.render(800000 + 110 + kTransitMs + pt::kRenderDelayMs, out, due);
    check(due.size() == 1, "its numbering starts over too");
}

void testClockWrap() {
    pt::Timeline timeline;
    constexpr uint32_t kBeforeWrap = 0xffffffffu - 20;
    arrive(timeline, poseAt(kBeforeWrap, 1, 0.0f));
    arrive(timeline, poseAt(kBeforeWrap + kSendGapMs, 2, 33.0f));  // wraps past zero
    pw::Pose out;
    std::vector<pw::MotionEvent> due;
    timeline.render(shownAt(kBeforeWrap) + 11, out, due);
    check(near(out.pos[0], 11.0f), "interpolation works across the 32-bit clock wrap");
}

}  // namespace

int main() {
    testNothingYet();
    testInterpolation();
    testNearestForDiscreteFields();
    testOrderAndDuplicates();
    testEvents();
    testStaleEvents();
    testClockTracking();
    testSenderClockRestart();
    testClockWrap();
    if (g_failures == 0) std::printf("pose_timeline_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
