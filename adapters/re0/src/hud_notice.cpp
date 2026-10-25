#include <algorithm>
#include <cmath>
#include <string>

#include "hud.h"
#include "hud_text.h"
#include "wait_notice_rule.h"

namespace {

using hud_text::Face;
using wait_notice_rule::Notice;

// A loading partner: small letters at the bottom over a hairline with a bright segment sweeping along it.
constexpr float kLoadingBaseline = 56.0f;  // above the screen's bottom edge
constexpr float kLoadingSize = 14.0f;
constexpr float kLoadingSpacing = 0.32f;   // of the size
constexpr uint32_t kLoadingRgb = 0xE0D6BA;
constexpr float kLoadingRuleGap = 10.0f;
constexpr float kLoadingRuleWidth = 140.0f;
constexpr float kLoadingRuleOpacity = 0.18f;
constexpr float kSegmentWidth = 46.0f;
constexpr float kSegmentOpacity = 0.85f;

// A partner's pause or message: the screen dims and the words stand between two hairlines.
constexpr ImU32 kVeil = IM_COL32(0, 0, 0, 140);  // 0.55
constexpr float kHeldSize = 20.0f;
constexpr float kHeldSpacing = 0.34f;
constexpr uint32_t kHeldRgb = 0xECE2C6;
constexpr float kHeldRuleWidth = 220.0f;
constexpr uint32_t kHeldRuleRgb = 0xB09660;
constexpr float kHeldRuleOpacity = 0.55f;
constexpr float kHeldGap = 10.0f;

// Text centred on `centreX` without the spacing that trails its last letter.
ImVec2 centredAt(float centreX, float top, const ImVec2& extent, float spacing) { return ImVec2(std::floor(centreX - (extent.x - spacing) * 0.5f), top); }

void drawLoading(const hud::Frame& frame, const std::string& text) {
    const float s = frame.scale;
    const float size = kLoadingSize * s;
    const float spacing = kLoadingSpacing * size;
    const float opacity = wait_notice_rule::pulseAlpha(frame.seconds);
    const ImVec2 extent = hud_text::measure(Face::CinzelMedium, size, text, spacing);
    const float baseline = frame.display.y - kLoadingBaseline * s;
    const ImVec2 at = centredAt(frame.display.x * 0.5f, baseline - hud_text::ascent(Face::CinzelMedium, size), extent, spacing);
    hud_text::draw(frame.list, Face::CinzelMedium, size, at, text, spacing, hud_text::colour(kLoadingRgb, opacity), hud_text::pixel(s));
    const float width = kLoadingRuleWidth * s;
    const float x = std::floor((frame.display.x - width) * 0.5f);
    const float y = baseline + kLoadingRuleGap * s;
    const float stroke = hud_text::pixel(s);
    frame.list->AddLine(ImVec2(x, y), ImVec2(x + width, y), hud_text::colour(kLoadingRgb, kLoadingRuleOpacity * opacity), stroke);
    const float start = x + wait_notice_rule::sweepStart(frame.seconds, width, kSegmentWidth * s);
    frame.list->AddLine(ImVec2(start, y), ImVec2(start + kSegmentWidth * s, y), hud_text::colour(kLoadingRgb, kSegmentOpacity * opacity), stroke);
}

void drawHeld(const hud::Frame& frame, const std::string& text) {
    const float s = frame.scale;
    frame.list->AddRectFilled(ImVec2(0.0f, 0.0f), frame.display, kVeil);
    const float size = kHeldSize * s;
    const float spacing = kHeldSpacing * size;
    const ImVec2 extent = hud_text::measure(Face::CinzelBold, size, text, spacing);
    const float stroke = hud_text::pixel(s);
    const float gap = kHeldGap * s;
    const float top = std::floor((frame.display.y - (extent.y + 2.0f * gap)) * 0.5f);
    const float width = kHeldRuleWidth * s;
    const float x = std::floor((frame.display.x - width) * 0.5f);
    const ImU32 rule = hud_text::colour(kHeldRuleRgb, kHeldRuleOpacity);
    frame.list->AddLine(ImVec2(x, top), ImVec2(x + width, top), rule, stroke);
    hud_text::draw(frame.list, Face::CinzelBold, size, centredAt(frame.display.x * 0.5f, top + gap, extent, spacing), text, spacing,
                   hud_text::colour(kHeldRgb, 1.0f), stroke);
    const float bottom = top + 2.0f * gap + extent.y;
    frame.list->AddLine(ImVec2(x, bottom), ImVec2(x + width, bottom), rule, stroke);
}

}  // namespace

namespace hud {

void drawWaitNotice(const Frame& frame, const wait_notice::Shown& shown) {
    const std::string text = hud_text::upper(shown.text);
    if (shown.notice == Notice::PartnerLoading) drawLoading(frame, text);
    else drawHeld(frame, text);
}

}  // namespace hud
