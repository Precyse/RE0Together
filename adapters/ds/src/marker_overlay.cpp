#include "marker_overlay.h"

#include <atomic>
#include <cstdio>
#include <string>

#include "game.h"
#include "imgui.h"
#include "player_sync.h"

namespace {

constexpr double kHeadHeight = 2.0;  // metres above the reported feet position
constexpr float kDiamondRadius = 9.0f;
constexpr float kOutline = 2.0f;
constexpr float kLabelSize = 20.0f;
constexpr float kLabelGap = 6.0f;
constexpr float kLabelPadding = 4.0f;
constexpr ImU32 kFill = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kEdge = IM_COL32(20, 20, 20, 255);
constexpr ImU32 kLabelBack = IM_COL32(0, 0, 0, 170);
constexpr ImU32 kLabelText = IM_COL32(240, 240, 240, 255);

std::atomic<bool> g_selfMarker{false};

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

}  // namespace

namespace marker_overlay {

void setSelfMarker(bool enabled) { g_selfMarker = enabled; }

void draw(float width, float height) {
    const auto camera = game::camera();
    if (!camera) return;
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    for (const player_sync::RemotePlayer& peer : player_sync::remotePlayers()) {
        drawMarker(list, *camera, peer.position, peer.name, width, height);
    }
    if (!g_selfMarker.load()) return;
    if (const auto self = game::localPlayer()) drawMarker(list, *camera, self->position, "local", width, height);
}

}  // namespace marker_overlay
