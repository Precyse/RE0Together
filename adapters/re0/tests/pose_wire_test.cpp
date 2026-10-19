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
    pose.loadSerial = 0xbeef;
    pose.x1034 = 0x1234;
    pose.hitAttr = 0x2a5;
    pose.manualPitch = -0.35f;
    pose.aimTier = 2;
    pose.held = true;
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
    check(back.loadSerial == 0xbeef && back.x1034 == 0x1234 && back.hitAttr == 0x2a5 && back.aimTier == 2 && back.held,
          "load serial, +0x1034, hit attributes, aim tier and the held flag survive");
    check(std::fabs(back.manualPitch - pose.manualPitch) <= 1.0f / pw::kPitchFixedScale, "aim pitch keeps 1/4096 radian precision");
    pw::Pose live = pose;
    live.held = false;
    check(!pw::decode(pw::encode(live)).held, "a live pose is not held");
    pw::Pose none = pose;
    none.layer[1] = {pw::kNoMotion, 0.0f};
    check(pw::decode(pw::encode(none)).layer[1].motion == pw::kNoMotion, "no motion on layer 1 survives");
}

void testPitch() {
    check(pw::packPitch(0.0f) == 0, "pitch 0 packs to 0");
    check(std::fabs(pw::unpackPitch(pw::packPitch(1.2345f)) - 1.2345f) <= 1.0f / pw::kPitchFixedScale, "pitch keeps 1/4096 radian");
    check(pw::packPitch(100.0f) == 32767 && pw::packPitch(-100.0f) == -32768, "a huge pitch saturates");
}

void testLayerTarget() {
    check(pw::layerOf(pw::packLayer(5, 3)) == 5 && pw::targetOf(pw::packLayer(5, 3)) == 3, "layer and target share the layer field");
    check(pw::packLayer(2, pw::kBodyTarget) == 2, "a body request is the plain layer number");
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

    const auto refuses = [&](const pw::PoseState& state, const char* what) {
        std::memcpy(bytes, &state, sizeof(state));
        check(!pw::decodePose(bytes).has_value(), what);
    };
    bad = wire;
    bad.group = pw::kMaxThinkGroup + 1;
    refuses(bad, "a think group over 3 is refused");
    bad = wire;
    bad.weapon = pw::kMaxWeaponType + 1;
    refuses(bad, "a weapon type over 0x1b is refused");
    bad = wire;
    bad.aimTier = pw::kMaxAimTier + 1;
    refuses(bad, "an aim tier over 2 is refused");
    bad = wire;
    bad.hitAttr = pw::kHitAttributeMask + 1;
    refuses(bad, "hit attributes over 10 bits are refused");
    bad = wire;
    bad.flags = 0x80;
    refuses(bad, "an unknown flag is refused");
    bad = wire;
    bad.weapon = pw::kMaxWeaponType;
    bad.aimTier = pw::kMaxAimTier;
    bad.group = pw::kMaxThinkGroup;
    std::memcpy(bytes, &bad, sizeof(bad));
    check(pw::decodePose(bytes).has_value(), "the limits themselves are accepted");
}

void testPoseSize() {
    static_assert(sizeof(pw::PoseState) == 56);
    check(sizeof(pw::PoseState) == 56, "POSE_STATE v2 is 56 bytes");
    const pw::PoseState wire = pw::encode(samplePose());
    check(wire.pos[0] == 10.5f && wire.seq == 77 && wire.senderMs == 123456, "senderMs, seq and position lead the payload");
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
    event.layer = pw::packLayer(pw::kMotionLayers - 1, pw::kSubControls);
    std::memcpy(bytes, &event, sizeof(event));
    check(pw::decodeEvent(bytes).has_value(), "the last layer of the last sub-control is accepted");
    event.layer = pw::packLayer(0, pw::kSubControls + 1);
    std::memcpy(bytes, &event, sizeof(event));
    check(!pw::decodeEvent(bytes).has_value(), "an anim control the character does not have is refused");
    event.layer = pw::kMotionLayers - 1;
    event.interp = std::nanf("");
    std::memcpy(bytes, &event, sizeof(event));
    check(!pw::decodeEvent(bytes).has_value(), "a NaN interpolation length is refused");
}

}  // namespace

int main() {
    testQuaternion();
    testFrame();
    testPitch();
    testLayerTarget();
    testPoseRoundTrip();
    testPoseSize();
    testPoseValidation();
    testEventValidation();
    if (g_failures == 0) std::printf("pose_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
