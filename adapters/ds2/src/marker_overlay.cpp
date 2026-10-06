#include "marker_overlay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "cargo_transfer.h"
#include "game.h"
#include "imgui.h"
#include "load_overlay.h"
#include "partner_status.h"
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
constexpr float kArrowSize = 12.0f;   // half the arrow's length and width
constexpr float kEdgeMargin = 70.0f;  // pixels from the screen edge: nearer than this the partner counts as off screen
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

void drawLabel(ImDrawList* list, ImVec2 at, const char* text, ImVec2 size) {
    list->AddRectFilled(ImVec2(at.x - kLabelPadding, at.y - kLabelPadding),
                        ImVec2(at.x + size.x + kLabelPadding, at.y + size.y + kLabelPadding), kLabelBack);
    list->AddText(ImGui::GetFont(), kLabelSize, at, kLabelText, text);
}

// "Name  42 m  80%  DEAD": distance, then health and state when the partner reports them.
std::string labelText(const std::string& name, float distance, const partner_status::Status& status) {
    char text[128];
    int length = std::snprintf(text, sizeof(text), "%s  %.0f m", name.c_str(), distance);
    const int percent = partner_status::healthPercent(status);
    if (percent >= 0) length += std::snprintf(text + length, sizeof(text) - length, "  %d%%", percent);
    const std::string state = partner_status::stateText(status);
    if (!state.empty()) std::snprintf(text + length, sizeof(text) - length, "  %s", state.c_str());
    return text;
}

float distanceTo(const world_to_screen::Camera& camera, const world_to_screen::Vec3& point) {
    const world_to_screen::Vec3 d = point - camera.position;
    return static_cast<float>(std::sqrt(world_to_screen::dot(d, d)));
}

// The partner is off screen: a triangle on the screen edge pointing the way, with the label tucked inside it.
void drawEdgeArrow(ImDrawList* list, const world_to_screen::EdgeArrow& arrow, const std::string& text, float width, float height) {
    const ImVec2 dir(std::cos(arrow.angle), std::sin(arrow.angle));
    const ImVec2 side(-dir.y, dir.x);
    const ImVec2 tip(arrow.x + dir.x * kArrowSize, arrow.y + dir.y * kArrowSize);
    const ImVec2 baseA(arrow.x - dir.x * kArrowSize + side.x * kArrowSize, arrow.y - dir.y * kArrowSize + side.y * kArrowSize);
    const ImVec2 baseB(arrow.x - dir.x * kArrowSize - side.x * kArrowSize, arrow.y - dir.y * kArrowSize - side.y * kArrowSize);
    list->AddTriangleFilled(tip, baseA, baseB, kFill);
    list->AddTriangle(tip, baseA, baseB, kEdge, kOutline);

    const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(kLabelSize, FLT_MAX, 0.0f, text.c_str());
    const float inset = 2 * kArrowSize + kLabelGap + std::fabs(dir.x) * size.x * 0.5f + std::fabs(dir.y) * size.y * 0.5f;
    const ImVec2 centre(arrow.x - dir.x * inset, arrow.y - dir.y * inset);
    const ImVec2 at(std::clamp(centre.x - size.x * 0.5f, kLabelPadding, width - size.x - kLabelPadding),
                    std::clamp(centre.y - size.y * 0.5f, kLabelPadding, height - size.y - kLabelPadding));
    drawLabel(list, at, text.c_str(), size);
}

void drawMarker(ImDrawList* list, const world_to_screen::Camera& camera, const world_to_screen::Vec3& feet,
                const std::string& name, const partner_status::Status& status, float width, float height) {
    const world_to_screen::Vec3 head{feet.x, feet.y, feet.z + kHeadHeight};
    if (const auto arrow = world_to_screen::edgeArrow(camera, head, width, height, kEdgeMargin)) {
        drawEdgeArrow(list, *arrow, labelText(name, distanceTo(camera, head), status), width, height);
        return;
    }
    const auto point = world_to_screen::project(camera, head, width, height);
    if (!point) return;
    const ImVec2 c(point->x, point->y);
    const ImVec2 top(c.x, c.y - kDiamondRadius), right(c.x + kDiamondRadius, c.y);
    const ImVec2 bottom(c.x, c.y + kDiamondRadius), left(c.x - kDiamondRadius, c.y);
    list->AddQuadFilled(top, right, bottom, left, kFill);
    list->AddQuad(top, right, bottom, left, kEdge, kOutline);

    const std::string text = labelText(name, point->distance, status);
    const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(kLabelSize, FLT_MAX, 0.0f, text.c_str());
    drawLabel(list, ImVec2(c.x - size.x * 0.5f, c.y - kDiamondRadius - kLabelGap - size.y), text.c_str(), size);
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

void drawToasts(float width) { ::drawToasts(ImGui::GetBackgroundDrawList(), width); }

void draw(float width, float height) {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    ::drawToasts(list, width);
    const auto camera = game::camera();
    if (!camera) return;
    const auto partner = cargo_transfer::partner();
    for (const player_sync::RemotePlayer& peer : player_sync::remotePlayers()) {
        const world_to_screen::Vec3 at = smoothed(peer);
        remote_body::setTarget(peer.slot, {at, peer.yaw}, {peer.velocity[0], peer.velocity[1], peer.velocity[2]});
        if (partner && partner->slot == peer.slot && !remote_body::ownerKey()) {  // the body carries the real rack once it exists
            load_overlay::draw(list, *camera, at, peer.yaw, static_cast<int>(partner->cargo.size()), width, height);
        }
        drawMarker(list, *camera, at, peer.name, peer.status, width, height);
    }
    if (!g_selfMarker.load()) return;
    if (const auto self = game::localPlayer()) drawMarker(list, *camera, self->position, "local", {}, width, height);
}

}  // namespace marker_overlay
