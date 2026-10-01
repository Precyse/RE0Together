#include "marker_overlay.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <map>
#include <string>

#include "game.h"
#include "imgui.h"
#include "player_sync.h"
#include "position_blend.h"
#include "remote_body.h"
#include "toast_queue.h"

namespace {

constexpr double kHeadHeight = 1.75;  // metres above the entity origin (Sam's feet) to the top of his head
constexpr float kDiamondRadius = 9.0f;
constexpr float kOutline = 2.0f;
constexpr float kLabelSize = 20.0f;
constexpr float kLabelGap = 6.0f;
constexpr float kLabelPadding = 4.0f;
constexpr ImU32 kFill = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kEdge = IM_COL32(20, 20, 20, 255);
constexpr ImU32 kLabelBack = IM_COL32(0, 0, 0, 170);
constexpr ImU32 kLabelText = IM_COL32(240, 240, 240, 255);

constexpr float kToastTop = 40.0f;
constexpr float kToastSpacing = 8.0f;
constexpr float kSnapMetres = 10.0f;  // a jump this large (teleport, respawn) moves the marker at once

std::atomic<bool> g_selfMarker{false};
std::map<uint8_t, std::array<float, 3>> g_shown;  // per peer slot: the position drawn last frame (render thread)

// Moves the drawn position part of the way toward the peer's extrapolated position each frame.
world_to_screen::Vec3 smoothed(const player_sync::RemotePlayer& peer) {
    auto [it, fresh] = g_shown.try_emplace(peer.slot, std::array<float, 3>{peer.position[0], peer.position[1], peer.position[2]});
    float shown[3] = {it->second[0], it->second[1], it->second[2]};
    if (!fresh && position_blend::distance(shown, peer.position) <= kSnapMetres) {
        float blended[3];
        position_blend::blendPosition(shown, peer.position, blended);
        it->second = {blended[0], blended[1], blended[2]};
    } else {
        it->second = {peer.position[0], peer.position[1], peer.position[2]};
    }
    return {it->second[0], it->second[1], it->second[2]};
}

void drawMarker(ImDrawList* list, const world_to_screen::Camera& camera, const world_to_screen::Vec3& feet,
                const std::string& name, float width, float height) {
    const world_to_screen::Vec3 head{feet.x, feet.y, feet.z + kHeadHeight};
    const auto point = world_to_screen::project(camera, head, width, height);
    if (!point) return;
    const ImVec2 c(point->x, point->y);
    const ImVec2 top(c.x, c.y - kDiamondRadius), right(c.x + kDiamondRadius, c.y);
    const ImVec2 bottom(c.x, c.y + kDiamondRadius), left(c.x - kDiamondRadius, c.y);
    list->AddQuadFilled(top, right, bottom, left, kFill);
    list->AddQuad(top, right, bottom, left, kEdge, kOutline);

    char label[96];
    std::snprintf(label, sizeof(label), "%s  %.0f m", name.c_str(), point->distance);
    ImFont* font = ImGui::GetFont();
    const ImVec2 size = font->CalcTextSizeA(kLabelSize, FLT_MAX, 0.0f, label);
    const ImVec2 at(c.x - size.x * 0.5f, c.y - kDiamondRadius - kLabelGap - size.y);
    list->AddRectFilled(ImVec2(at.x - kLabelPadding, at.y - kLabelPadding),
                        ImVec2(at.x + size.x + kLabelPadding, at.y + size.y + kLabelPadding), kLabelBack);
    list->AddText(font, kLabelSize, at, kLabelText, label);
}

// Session messages (peer joined / left), stacked top-centre, fading out.
void drawToasts(ImDrawList* list, float width) {
    ImFont* font = ImGui::GetFont();
    float y = kToastTop;
    for (const toast_queue::Visible& toast : toast_queue::visible()) {
        const ImVec2 size = font->CalcTextSizeA(kLabelSize, FLT_MAX, 0.0f, toast.text.c_str());
        const ImVec2 at((width - size.x) * 0.5f, y);
        const auto alpha = static_cast<int>(toast.alpha * 255.0f);
        list->AddRectFilled(ImVec2(at.x - kLabelPadding, at.y - kLabelPadding),
                            ImVec2(at.x + size.x + kLabelPadding, at.y + size.y + kLabelPadding),
                            IM_COL32(0, 0, 0, alpha * 170 / 255));
        list->AddText(font, kLabelSize, at, IM_COL32(240, 240, 240, alpha), toast.text.c_str());
        y += size.y + 2 * kLabelPadding + kToastSpacing;
    }
}

}  // namespace

namespace marker_overlay {

void setSelfMarker(bool enabled) { g_selfMarker = enabled; }

void draw(float width, float height) {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    drawToasts(list, width);
    const auto camera = game::camera();
    if (!camera) return;
    for (const player_sync::RemotePlayer& peer : player_sync::remotePlayers()) {
        const world_to_screen::Vec3 at = smoothed(peer);
        remote_body::setTarget(peer.slot, {at, peer.yaw}, {peer.velocity[0], peer.velocity[1], peer.velocity[2]});
        drawMarker(list, *camera, at, peer.name, width, height);
    }
    if (!g_selfMarker.load()) return;
    if (const auto self = game::localPlayer()) drawMarker(list, *camera, self->position, "local", width, height);
}

}  // namespace marker_overlay
