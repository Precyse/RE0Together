#pragma once
// Reads a body through the engine's own getters (calls.h, feature body): world position (via.Position, doubles),
// facing, the current action hash of the first two action layers, HP, death and the discrete locomotion speed. Game
// thread only.
#include <cmath>
#include <cstdint>
#include <optional>

namespace local_body {

constexpr uint32_t kSampledLayers = 2;

struct Sample {
    double position[3] = {};
    float yaw = 0.0f;  // radians around the up axis (Y)
    uint32_t actionHash[kSampledLayers] = {};
    float hp = 0.0f;
    bool dead = false;
    int32_t speedType = 0;  // app.LocomotionSpeedTypeEnum value (Idle, Walk, Run, Dash)
};

// The local body of a kind (wire_dd2 kKindArisen: the manual player; kKindMainPawn: the main pawn), or null.
void* character(uint8_t kind);

// Null while there is no such body (title, loading, no main pawn) or a getter failed.
std::optional<Sample> sample(uint8_t kind);

// The same for any character (a puppet's body).
std::optional<Sample> sampleCharacter(void* character);

// Yaw around Y (the engine's up axis) from a unit quaternion (x, y, z, w) (pure, unit tested).
inline float yawOf(const float q[4]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return std::atan2(2.0f * (w * y + x * z), 1.0f - 2.0f * (x * x + y * y));
}

}  // namespace local_body
