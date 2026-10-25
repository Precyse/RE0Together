#include <cmath>
#include <string>

#include "gfd_text.h"
#include "hud.h"
#include "hud_text.h"
#include "ui_art_files.h"
#include "ui_textures.h"

namespace {

using toast_queue::Style;

constexpr uint32_t kTextRgb = 0xE6DCC0;

// A line: the game's font on a soft dark band at the bottom, edges feathered by a few larger and fainter rectangles.
constexpr float kLineBottom = 70.0f;   // the text's bottom edge above the screen's bottom edge
constexpr float kBandPadX = 40.0f;
constexpr float kBandPadY = 8.0f;
constexpr int kFeatherLayers = 5;
constexpr float kFeatherStep = 5.0f;
constexpr float kLayerOpacity = 0.13f;  // five layers stacked reach 0.5 at the core
constexpr float kLineGap = 6.0f;

// A panel: the game's dialog leather between two rods at the top.
constexpr float kPanelTop = 36.0f;
constexpr float kPanelWidth = 520.0f;
constexpr float kRodTopHeight = 32.0f;
constexpr float kRodBottomHeight = 33.0f;
constexpr float kLeatherHeight = 52.0f;
constexpr float kLeatherInset = 6.0f;
constexpr float kLeatherOverlap = 22.0f;  // how far the leather runs under each rod
constexpr float kPanelGap = 8.0f;
constexpr uint32_t kFallbackRgb = 0x14100C;  // a plain dark panel while the dialog art is missing
constexpr float kFallbackOpacity = 0.78f;

std::wstring artPath(const wchar_t* file) { return std::wstring(ui_art_files::kArtFolder) + L"\\" + file; }

void drawImage(const hud::Frame& frame, const wchar_t* file, ImVec2 from, ImVec2 to, float alpha) {
    const ui_textures::Image& image = ui_textures::get(frame.device, artPath(file));
    if (!image.texture) return;
    const ImTextureRef texture(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(image.texture)));
    frame.list->AddImage(texture, from, to, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), hud_text::colour(0xFFFFFF, alpha));
}

bool artAvailable(const hud::Frame& frame) {
    return ui_textures::get(frame.device, artPath(ui_art_files::kLeather)).texture && ui_textures::get(frame.device, artPath(ui_art_files::kRodTop)).texture &&
           ui_textures::get(frame.device, artPath(ui_art_files::kRodBottom)).texture;
}

void drawText(const hud::Frame& frame, const std::string& text, ImVec2 at, float alpha) {
    gfd_text::draw(frame.list, at, text, frame.scale, hud_text::colour(kTextRgb, alpha), hud_text::pixel(frame.scale));
}

// Draws one line with its top edge `bottom` minus its height; returns the top edge.
float drawLine(const hud::Frame& frame, const toast_queue::Visible& toast, float bottom) {
    const float s = frame.scale;
    const ImVec2 extent = gfd_text::measure(toast.text, s);
    const ImVec2 at(std::floor((frame.display.x - extent.x) * 0.5f), bottom - extent.y);
    const float padX = kBandPadX * s;
    const float padY = kBandPadY * s;
    for (int layer = kFeatherLayers - 1; layer >= 0; --layer) {
        const float grow = static_cast<float>(layer) * kFeatherStep * s;
        const ImVec2 from(at.x - padX - grow, at.y - padY - grow);
        const ImVec2 to(at.x + extent.x + padX + grow, at.y + extent.y + padY + grow);
        frame.list->AddRectFilled(from, to, hud_text::colour(0x000000, kLayerOpacity * toast.alpha), (to.y - from.y) * 0.5f);
    }
    drawText(frame, toast.text, at, toast.alpha);
    return at.y - padY;
}

// Draws one panel with its top at `top`; returns its bottom.
float drawPanel(const hud::Frame& frame, const toast_queue::Visible& toast, float top) {
    const float s = frame.scale;
    const float left = std::floor((frame.display.x - kPanelWidth * s) * 0.5f);
    const float right = left + kPanelWidth * s;
    const float leatherTop = top + (kRodTopHeight - kLeatherOverlap) * s;
    const float leatherBottom = leatherTop + kLeatherHeight * s;
    const float bottom = leatherBottom - kLeatherOverlap * s + kRodBottomHeight * s;
    if (artAvailable(frame)) {
        drawImage(frame, ui_art_files::kLeather, ImVec2(left + kLeatherInset * s, leatherTop), ImVec2(right - kLeatherInset * s, leatherBottom), toast.alpha);
        drawImage(frame, ui_art_files::kRodTop, ImVec2(left, top), ImVec2(right, top + kRodTopHeight * s), toast.alpha);
        drawImage(frame, ui_art_files::kRodBottom, ImVec2(left, bottom - kRodBottomHeight * s), ImVec2(right, bottom), toast.alpha);
    } else {
        frame.list->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), hud_text::colour(kFallbackRgb, kFallbackOpacity * toast.alpha));
    }
    const ImVec2 extent = gfd_text::measure(toast.text, s);
    drawText(frame, toast.text, ImVec2(std::floor((left + right - extent.x) * 0.5f), std::floor((leatherTop + leatherBottom - extent.y) * 0.5f)), toast.alpha);
    return bottom;
}

}  // namespace

namespace hud {

void drawBanners(const Frame& frame, const std::vector<toast_queue::Visible>& toasts) {
    gfd_text::prepare(frame.device);
    float lineBottom = frame.display.y - kLineBottom * frame.scale;
    float panelTop = kPanelTop * frame.scale;
    for (const toast_queue::Visible& toast : toasts) {
        if (toast.style == Style::Line) lineBottom = drawLine(frame, toast, lineBottom) - kLineGap * frame.scale;
        else panelTop = drawPanel(frame, toast, panelTop) + kPanelGap * frame.scale;
    }
}

}  // namespace hud
