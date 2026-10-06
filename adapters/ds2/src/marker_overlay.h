#pragma once

// World-space markers for the other players: a diamond above each peer's head with their name and distance, and
// their load on their back (load_overlay), projected through the game's camera. Optionally marks the local player too
// (projection check).
namespace marker_overlay {

void setSelfMarker(bool enabled);

// Session messages, stacked top-centre.
void drawToasts(float width);

// dx12_hook draw callback (render thread).
void draw(float width, float height);

}  // namespace marker_overlay
