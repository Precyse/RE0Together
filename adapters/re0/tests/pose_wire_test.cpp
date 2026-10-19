// Checks the pose wire format (no game): quaternion packing, fixed-point frames, encode/decode and payload validation.
// Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/pose_wire.h"

namespace {

namespace pw = pose_wire;

constexpr float kQuatTolerance = 0.003f;
constexpr float kFrameTolerance = 1.0f / pw::kFrameFixedScale;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// Same rotation: the quaternions agree up to a sign.
bool sameRotation(const float (&a)[4], const float (&b)[4]) {
    float dot = 0.0f;
    for (int i = 0; i < 4; ++i) dot += a[i] * b[i];
    return std::fabs(std::fabs(dot) - 1.0f) < kQuatTolerance * kQuatTolerance;
}

void roundTrip(const float (&q)[4], const char* what) {
    float out[4];
    pw::unpackQuat(pw::packQuat(q), out);
    check(sameRotation(q, out), what);
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    check(std::fabs(length - 1.0f) < kQuatTolerance, "unpacked quaternion is a unit quaternion");
}

void testQuaternion() {
    const float identity[4] = {0, 0, 0, 1};
    roundTrip(identity, "identity survives packing");
    const float flipped[4] = {0, 0, 0, -1};
    roundTrip(flipped, "negative largest component survives packing");
    const float quarterY[4] = {0, std::sqrt(0.5f), 0, std::sqrt(0.5f)};
    roundTrip(quarterY, "90 degrees about y survives packing");
    const float tilted[4] = {0.5f, -0.5f, 0.5f, -0.5f};
    roundTrip(tilted, "four equal components survive packing");
    const float xLargest[4] = {-0.9f, 0.1f, 0.2f, std::sqrt(1.0f - 0.81f - 0.01f - 0.04f)};
    roundTrip(xLargest, "largest component in slot 0 survives packing");
    check((pw::packQuat(identity) >> pw::kQuatIndexShift) == 3, "the largest component's index is in the top bits");
}

void testFrame() {
    check(pw::packFrame(0.0f) == 0, "frame 0 packs to 0");
    check(std::fabs(pw::unpackFrame(pw::packFrame(12.34f)) - 12.34f) <= kFrameTolerance, "frame keeps 1/64 precision");
    check(pw::packFrame(-3.0f) == 0, "a negative frame packs to 0");
    check(pw::packFrame(1.0e9f) == 65535, "a huge frame saturates");
}

pw::Pose samplePose() {
    pw::Pose pose;
    pose.senderMs = 123456;
    pose.seq = 77;
    pose.room = 0x2f;
    pose.pos[0] = 10.5f;
    pose.pos[1] = -2.0f;
    pose.pos[2] = 300.25f;
    pose.quat[1] = std::sqrt(0.5f);
    pose.quat[3] = std::sqrt(0.5f);
    pose.layer[0] = {0x209, 41.5f};
    pose.layer[1] = {0x34, 7.25f};
    pose.group = 1;
    pose.state = 5;
    pose.phase = 2;
    pose.weapon = 9;
    pose.status = 0xdeadbeef;
    return pose;
}

void testPoseRoundTrip() {
    const pw::Pose pose = samplePose();
    const pw::Pose back = pw::decode(pw::encode(pose));
    check(back.senderMs == pose.senderMs && back.seq == pose.seq && back.room == pose.room, "header fields survive");
    check(back.pos[0] == pose.pos[0] && back.pos[1] == pose.pos[1] && back.pos[2] == pose.pos[2], "position survives exactly");
    check(sameRotation(back.quat, pose.quat), "rotation survives");
    check(back.layer[0].motion == 0x209 && back.layer[0].frame == 41.5f, "layer 0 survives exactly");
    check(back.layer[1].motion == 0x34 && std::fabs(back.layer[1].frame - 7.25f) <= kFrameTolerance, "layer 1 survives");
    check(back.group == 1 && back.state == 5 && back.phase == 2 && back.weapon == 9 && back.status == 0xdeadbeef,
          "think, weapon and status survive");
    pw::Pose none = pose;
    none.layer[1] = {pw::kNoMotion, 0.0f};
    check(pw::decode(pw::encode(none)).layer[1].motion == pw::kNoMotion, "no motion on layer 1 survives");
}

void testPoseValidation() {
    const pw::PoseState wire = pw::encode(samplePose());
    uint8_t bytes[sizeof(wire)];
    std::memcpy(bytes, &wire, sizeof(wire));
    check(pw::decodePose(bytes).has_value(), "a well-formed POSE_STATE decodes");
    check(!pw::decodePose(std::span<const uint8_t>(bytes, sizeof(wire) - 1)).has_value(), "a short POSE_STATE is refused");
    pw::PoseState bad = wire;
    bad.pos[1] = std::nanf("");
    std::memcpy(bytes, &bad, sizeof(bad));
    check(!pw::decodePose(bytes).has_value(), "a NaN position is refused");
    bad = wire;
    bad.frame0 = INFINITY;
    std::memcpy(bytes, &bad, sizeof(bad));
    check(!pw::decodePose(bytes).has_value(), "an infinite frame is refused");
}

void testEventValidation() {
    pw::MotionEvent event{1000, 0x11, 4.0f, 12.0f, 5, 0x209, 0};
    uint8_t bytes[sizeof(event)];
    std::memcpy(bytes, &event, sizeof(event));
    const auto decoded = pw::decodeEvent(bytes);
    check(decoded && decoded->motionNo == 0x209 && decoded->start == 12.0f && decoded->seq == 5,
          "a well-formed MOTION_EVENT decodes");
    check(!pw::decodeEvent(std::span<const uint8_t>(bytes, sizeof(event) - 4)).has_value(), "a short MOTION_EVENT is refused");
    event.layer = pw::kMotionLayers;
    std::memcpy(bytes, &event, sizeof(event));
    check(!pw::decodeEvent(bytes).has_value(), "a layer the engine does not have is refused");
    event.layer = pw::kMotionLayers - 1;
    event.interp = std::nanf("");
    std::memcpy(bytes, &event, sizeof(event));
    check(!pw::decodeEvent(bytes).has_value(), "a NaN interpolation length is refused");
}

}  // namespace

int main() {
    testQuaternion();
    testFrame();
    testPoseRoundTrip();
    testPoseValidation();
    testEventValidation();
    if (g_failures == 0) std::printf("pose_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
