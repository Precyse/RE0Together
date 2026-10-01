#pragma once
// Decima engine types shared by every Decima game (layouts from the engine's own RTTI, see docs/DS_NOTES.md).
#include <cstddef>
#include <cstdint>

namespace decima {

// WorldPosition: double precision so large open worlds keep millimetre accuracy.
struct WorldPosition {
    double x, y, z;
};
static_assert(sizeof(WorldPosition) == 0x18);

// RotMatrix: three rows of three floats (the entity's right, forward and up axes).
struct RotMatrix {
    float row[3][3];
};
static_assert(sizeof(RotMatrix) == 0x24);

struct WorldTransform {
    WorldPosition position;
    RotMatrix orientation;
    uint32_t padding;
};
static_assert(sizeof(WorldTransform) == 0x40);
static_assert(offsetof(WorldTransform, orientation) == 0x18);

}  // namespace decima
