#include <cmath>
#include <string>

#include "coop_scene_rule.h"
#include "gfd_text.h"
#include "hud.h"
#include "hud_text.h"
#include "ui_art_files.h"
#include "ui_textures.h"

namespace {

constexpr float kPictureHeight = 470.0f;
constexpr float kPictureTop = 150.0f;
constexpr float kHostLeft = 40.0f;
constexpr float kGuestRightMargin = 44.0f;
constexpr float kCaptionTop = 468.0f;   // inside the frame's window, under the rows
constexpr float kCaptionLeft = 294.0f;    // the host's captions start here, under the row labels
constexpr float kCaptionRight = 990.0f;   // the guest's captions end here, under the values
constexpr uint32_t kNameRgb = 0xE6DCC0;
constexpr uint32_t kDimRgb = 0x7D7764;

std::wstring picturePath(const std::string& picture) {
    return std::wstring(ui_art_files::kArtFolder) + L"\\" + ui_art_files::kOutfitFolder + L"\\" + std::wstring(picture.begin(), picture.end()) +
           ui_art_files::kOutfitExtension;
}

// The character's picture: its height fixed, its width following the picture; an open seat is a black silhouette.
// `rightAligned` puts the picture's right edge at `x`, else its left edge.
void drawPicture(const hud::Frame& frame, const coop_scene::Side& side, float x, bool rightAligned) {
    if (side.picture.empty()) return;
    const ui_textures::Image& image = ui_textures::get(frame.device, picturePath(side.picture));
    if (!image.texture) return;
    const float s = frame.scale;
    const float height = kPictureHeight * s;
    const float width = height * static_cast<float>(image.width) / static_cast<float>(image.height);
    const float left = rightAligned ? x - width : x;
    const float top = kPictureTop * s;
    const ImU32 tint = side.open ? hud_text::colour(0x000000, coop_scene_rule::silhouetteAlpha(frame.seconds)) : hud_text::colour(0xFFFFFF, 1.0f);
    const ImTextureRef texture(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(image.texture)));
    frame.list->AddImage(texture, ImVec2(left, top), ImVec2(left + width, top + height), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
}

// The name and the line under it, from `x` rightward or ending at `x`.
void drawCaption(const hud::Frame& frame, const coop_scene::Side& side, float x, bool rightAligned) {
    if (side.name.empty()) return;
    const float s = frame.scale;
    const float shadow = hud_text::pixel(s);
    const float top = kCaptionTop * s;
    const ImU32 nameColour = hud_text::colour(side.open ? kDimRgb : kNameRgb, 1.0f);
    const ImVec2 nameExtent = gfd_text::measure(side.name, s);
    const ImVec2 roleExtent = gfd_text::measure(side.role, s);
    gfd_text::draw(frame.list, ImVec2(std::floor(rightAligned ? x - nameExtent.x : x), top), side.name, s, nameColour, shadow);
    gfd_text::draw(frame.list, ImVec2(std::floor(rightAligned ? x - roleExtent.x : x), top + nameExtent.y), side.role, s, hud_text::colour(kDimRgb, 1.0f),
                   shadow);
}

}  // namespace

namespace hud {

void drawCoopScene(const Frame& frame, const coop_scene::Scene& scene) {
    gfd_text::prepare(frame.device);
    const float s = frame.scale;
    drawPicture(frame, scene.host, kHostLeft * s, false);
    drawPicture(frame, scene.guest, frame.display.x - kGuestRightMargin * s, true);
    drawCaption(frame, scene.host, kCaptionLeft * s, false);
    drawCaption(frame, scene.guest, kCaptionRight * s, true);
}

}  // namespace hud
