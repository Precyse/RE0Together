#pragma once
// The partner's load drawn on their body: one box per carried piece, stacked up their back and projected through the
// game's camera (cargo_transfer keeps the list each player reports). A picture only: no physics, no weight.
#include "game.h"
#include "imgui.h"

namespace load_overlay {

// Render thread, from marker_overlay: a body standing at `feet`, facing `yaw`, carrying `pieces` pieces.
void draw(ImDrawList* list, const world_to_screen::Camera& camera, const world_to_screen::Vec3& feet, float yaw,
          int pieces, float width, float height);

}  // namespace load_overlay
