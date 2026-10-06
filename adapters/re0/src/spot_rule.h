#pragma once
#include <cmath>
#include <cstdint>

// Pure rule for which of a door entry's spots a character is put on (no game access, unit tested). Each entry has a
// spot per mode: 0 the one coming through the door, 1 beside it, 2 a following partner behind it. Two characters
// placed on the same spot are pushed apart by the collision, hard enough to throw one into the ceiling.
namespace spot_rule {

constexpr int kModes = 3;
constexpr uint32_t kDoorMode = 0;
constexpr uint32_t kSideMode = 1;
constexpr uint32_t kFollowMode = 2;
constexpr float kMinSeparation = 30.0f;  // spots closer than this still overlap two characters

struct Spot {
    float x, y, z;
};

inline float distance(const Spot& a, const Spot& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// The mode to place a character in: the door spot when it comes alone, else the follower spot, else the side spot,
// else (spots all alike) the door spot.
inline uint32_t modeFor(bool otherInRoom, const Spot (&spots)[kModes]) {
    if (!otherInRoom) return kDoorMode;
    if (distance(spots[kFollowMode], spots[kDoorMode]) >= kMinSeparation) return kFollowMode;
    if (distance(spots[kSideMode], spots[kDoorMode]) >= kMinSeparation) return kSideMode;
    return kDoorMode;
}

}  // namespace spot_rule
