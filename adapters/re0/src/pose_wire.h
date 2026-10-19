#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

#include "protocol.h"
#include "settled_copy.h"

// Wire format of the pose puppet (partner_mode=state, docs/RE0_NOTES.md "Pose puppet"): the owner's character sampled
// after its own move (POSE_STATE) and the motion requests its engine made (MOTION_EVENT). Pure, no game access.
namespace pose_wire {

constexpr uint16_t kMsgPoseState = proto::kFirstGameType + 0x40;    // 0x0140, owner to all, unreliable
constexpr uint16_t kMsgMotionEvent = proto::kFirstGameType + 0x41;  // 0x0141, owner to all, reliable, numbered

// The clock both streams carry: milliseconds of the steady clock, wrapping at 32 bits.
inline uint32_t clockMs() { return static_cast<uint32_t>(monotonicMs()); }

constexpr uint16_t kNoMotion = 0xffff;  // the engine's "no motion" number
constexpr uint16_t kMotionLayers = 8;   // uModel mMotion[8]
constexpr uint16_t kBodyTarget = 0;     // MotionEvent target: the character's body anim control
constexpr uint16_t kSubControls = 7;    // a character's weapon and accessory anim controls (+0x67dc + 0x20 * k)

// Limits the decoders enforce.
constexpr uint8_t kMaxThinkGroup = 3;
constexpr uint8_t kMaxWeaponType = 0x1b;
constexpr uint8_t kMaxAimTier = 2;
constexpr uint16_t kHitAttributeMask = 0x3ff;

constexpr uint8_t kFlagHeld = 0x1;  // a resend of the last pose: the owner's world is not moving (menu, door, load)
constexpr uint8_t kKnownFlags = kFlagHeld;

// One motion layer of a pose: motion number and the current frame.
struct Layer {
    uint16_t motion = kNoMotion;
    float frame = 0.0f;
};

// A sampled character, in host units.
struct Pose {
    uint32_t senderMs = 0;
    uint32_t seq = 0;
    uint16_t room = 0;
    float pos[3] = {};
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Layer layer[2];  // layer 0 (body) and layer 1 (upper body / overlay)
    uint8_t group = 0;
    uint8_t state = 0;
    uint8_t phase = 0;
    uint8_t weapon = 0;
    uint32_t status = 0;
    uint16_t loadSerial = 0;
    uint32_t x1034 = 0;
    uint16_t hitAttr = 0;
    float manualPitch = 0.0f;  // radians
    uint8_t aimTier = 0;
    bool held = false;
};

// Wire payload of POSE_STATE (0x0140): 30 Hz in play, resent at 4 Hz with `held` while the owner's world does not move.
struct PoseState {
    uint32_t senderMs;
    uint32_t seq;
    float pos[3];
    uint32_t quat;    // smallest three: 2 bits index of the dropped component, 3 x 10 bits
    float frame0;
    uint32_t status;  // the character's status flags (+0x68c4)
    uint32_t x1034;   // uCharaBase +0x1034
    uint16_t room;
    uint16_t loadSerial;
    uint16_t motion0;
    uint16_t motion1;
    uint16_t frame1;  // fixed point, 1/64 frame
    uint16_t hitAttr;
    int16_t manualPitch;  // fixed point, 1/4096 radian
    uint8_t group;        // think group / state / phase
    uint8_t state;
    uint8_t phase;
    uint8_t weapon;       // held weapon type
    uint8_t aimTier;
    uint8_t flags;
};
static_assert(sizeof(PoseState) == 56);
static_assert(std::is_trivially_copyable_v<PoseState>);

// Wire payload of MOTION_EVENT (0x0141): the arguments of one cAnimControl::set call the owner's engine made.
struct MotionEvent {
    uint32_t senderMs;
    uint32_t flags;
    float interp;
    float start;
    uint32_t seq;
    uint16_t motionNo;
    uint16_t layer;  // low byte: the layer (< 8); high byte: the anim control, 0 = the body, 1 + k = sub-control k (< 7)
};
static_assert(sizeof(MotionEvent) == 24);
static_assert(std::is_trivially_copyable_v<MotionEvent>);

constexpr int kQuatComponents = 4;
constexpr int kQuatBits = 10;
constexpr uint32_t kQuatMax = (1u << kQuatBits) - 1;
constexpr int kQuatIndexShift = 3 * kQuatBits;
constexpr float kQuatRange = 0.70710678f;  // the three smaller components of a unit quaternion stay within +-1/sqrt(2)
constexpr float kFrameFixedScale = 64.0f;
constexpr float kPitchFixedScale = 4096.0f;
constexpr uint16_t kLayerByteMask = 0xff;
constexpr int kTargetShift = 8;

// The two halves of MotionEvent::layer.
inline uint16_t packLayer(uint16_t layer, uint16_t target) { return static_cast<uint16_t>((target << kTargetShift) | layer); }
inline uint16_t layerOf(uint16_t packed) { return packed & kLayerByteMask; }
inline uint16_t targetOf(uint16_t packed) { return static_cast<uint16_t>(packed >> kTargetShift); }

inline uint32_t packQuat(const float (&q)[4]) {
    int largest = 0;
    for (int i = 1; i < kQuatComponents; ++i) {
        if (std::fabs(q[i]) > std::fabs(q[largest])) largest = i;
    }
    const float sign = q[largest] < 0.0f ? -1.0f : 1.0f;  // the dropped component is rebuilt positive
    uint32_t packed = static_cast<uint32_t>(largest) << kQuatIndexShift;
    int shift = 0;
    for (int i = 0; i < kQuatComponents; ++i) {
        if (i == largest) continue;
        const float unit = std::clamp(sign * q[i] / kQuatRange * 0.5f + 0.5f, 0.0f, 1.0f);
        packed |= static_cast<uint32_t>(std::lround(unit * kQuatMax)) << shift;
        shift += kQuatBits;
    }
    return packed;
}

inline void unpackQuat(uint32_t packed, float (&q)[4]) {
    const int largest = static_cast<int>(packed >> kQuatIndexShift);
    float sumSquares = 0.0f;
    int shift = 0;
    for (int i = 0; i < kQuatComponents; ++i) {
        if (i == largest) continue;
        const float unit = static_cast<float>((packed >> shift) & kQuatMax) / kQuatMax;
        q[i] = (unit - 0.5f) * 2.0f * kQuatRange;
        sumSquares += q[i] * q[i];
        shift += kQuatBits;
    }
    q[largest] = std::sqrt(std::max(0.0f, 1.0f - sumSquares));
}

inline uint16_t packFrame(float frame) {
    return static_cast<uint16_t>(std::lround(std::clamp(frame * kFrameFixedScale, 0.0f, 65535.0f)));
}

inline float unpackFrame(uint16_t fixed) { return static_cast<float>(fixed) / kFrameFixedScale; }

inline int16_t packPitch(float radians) {
    return static_cast<int16_t>(std::lround(std::clamp(radians * kPitchFixedScale, -32768.0f, 32767.0f)));
}

inline float unpackPitch(int16_t fixed) { return static_cast<float>(fixed) / kPitchFixedScale; }

inline PoseState encode(const Pose& pose) {
    PoseState wire{};
    wire.senderMs = pose.senderMs;
    wire.seq = pose.seq;
    std::memcpy(wire.pos, pose.pos, sizeof(wire.pos));
    wire.quat = packQuat(pose.quat);
    wire.frame0 = pose.layer[0].frame;
    wire.status = pose.status;
    wire.x1034 = pose.x1034;
    wire.room = pose.room;
    wire.loadSerial = pose.loadSerial;
    wire.motion0 = pose.layer[0].motion;
    wire.motion1 = pose.layer[1].motion;
    wire.frame1 = packFrame(pose.layer[1].frame);
    wire.hitAttr = pose.hitAttr;
    wire.manualPitch = packPitch(pose.manualPitch);
    wire.group = pose.group;
    wire.state = pose.state;
    wire.phase = pose.phase;
    wire.weapon = pose.weapon;
    wire.aimTier = pose.aimTier;
    wire.flags = pose.held ? kFlagHeld : 0;
    return wire;
}

inline Pose decode(const PoseState& wire) {
    Pose pose;
    pose.senderMs = wire.senderMs;
    pose.seq = wire.seq;
    pose.room = wire.room;
    pose.loadSerial = wire.loadSerial;
    std::memcpy(pose.pos, wire.pos, sizeof(pose.pos));
    unpackQuat(wire.quat, pose.quat);
    pose.layer[0] = {wire.motion0, wire.frame0};
    pose.layer[1] = {wire.motion1, unpackFrame(wire.frame1)};
    pose.group = wire.group;
    pose.state = wire.state;
    pose.phase = wire.phase;
    pose.weapon = wire.weapon;
    pose.status = wire.status;
    pose.x1034 = wire.x1034;
    pose.hitAttr = wire.hitAttr;
    pose.manualPitch = unpackPitch(wire.manualPitch);
    pose.aimTier = wire.aimTier;
    pose.held = (wire.flags & kFlagHeld) != 0;
    return pose;
}

inline bool finite(float value) { return std::isfinite(value); }

// A received POSE_STATE of the right size with finite numbers and every field in range, or nothing.
inline std::optional<Pose> decodePose(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(PoseState)) return std::nullopt;
    PoseState wire;
    std::memcpy(&wire, payload.data(), sizeof(wire));
    if (!finite(wire.pos[0]) || !finite(wire.pos[1]) || !finite(wire.pos[2]) || !finite(wire.frame0)) return std::nullopt;
    if (wire.group > kMaxThinkGroup || wire.weapon > kMaxWeaponType || wire.aimTier > kMaxAimTier ||
        wire.hitAttr > kHitAttributeMask || (wire.flags & ~kKnownFlags) != 0) {
        return std::nullopt;
    }
    return decode(wire);
}

// A received MOTION_EVENT of the right size, on a layer and anim control the engine has, with finite numbers, or nothing.
inline std::optional<MotionEvent> decodeEvent(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(MotionEvent)) return std::nullopt;
    MotionEvent event;
    std::memcpy(&event, payload.data(), sizeof(event));
    if (layerOf(event.layer) >= kMotionLayers || targetOf(event.layer) > kSubControls || !finite(event.interp) ||
        !finite(event.start)) {
        return std::nullopt;
    }
    return event;
}

}  // namespace pose_wire
