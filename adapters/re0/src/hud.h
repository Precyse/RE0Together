#pragma once
#include <d3d9.h>

#include <vector>

#include "coop_scene.h"
#include "imgui.h"
#include "partner_status.h"
#include "toast_queue.h"
#include "wait_notice.h"

// The in-game co-op HUD (docs/RE0_NOTES.md "Co-op HUD"), drawn by debug_overlay on the render thread into the foreground
// draw list: the partner tag, the wait notices, the banners and the characters beside the Co-op page. Positions and sizes
// are written at 720 lines and multiplied by the frame's scale.
namespace hud {

struct Frame {
    IDirect3DDevice9* device;
    ImDrawList* list;
    ImVec2 display;   // the backbuffer's size in pixels
    float scale;      // display height over 720
    float seconds;    // a steady clock for the animations
};

// Bottom left: the partner's name, a heartbeat trace in the colour of its condition and, while it is away, where.
void drawPartnerTag(const Frame& frame, const partner_status::Tag& tag);

// The wait notice: a loading partner at the bottom, a paused or busy one over a dimmed screen.
void drawWaitNotice(const Frame& frame, const wait_notice::Shown& shown);

// The banners: lines at the bottom, panels at the top.
void drawBanners(const Frame& frame, const std::vector<toast_queue::Visible>& toasts);

// The host's character on the left and the guest's on the right with their names.
void drawCoopScene(const Frame& frame, const coop_scene::Scene& scene);

}  // namespace hud
