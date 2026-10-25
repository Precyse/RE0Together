#include <string>
#include <vector>

#include "hud.h"
#include "hud_text.h"
#include "partner_tag_rule.h"

namespace {

using hud_text::Face;

constexpr float kLeft = 36.0f;
constexpr float kBottom = 30.0f;
constexpr float kOpacity = 0.75f;
constexpr float kNameSize = 13.0f;
constexpr float kNameSpacing = 0.2f;  // of the size
constexpr uint32_t kNameRgb = 0xE6DCC0;
constexpr float kTraceGap = 10.0f;
constexpr float kTraceStroke = 1.5f;
constexpr float kDetailSize = 14.0f;
constexpr float kDetailGap = 2.0f;
constexpr uint32_t kDetailRgb = 0xBFB397;

// The heartbeat sweeping across its box, the part drawn so far.
void drawTrace(const hud::Frame& frame, ImVec2 origin, partner_status::Condition condition) {
    const partner_tag_rule::Style style = partner_tag_rule::styleOf(condition);
    const std::vector<partner_tag_rule::Point> points =
        partner_tag_rule::visible(partner_tag_rule::trace(style.beats), partner_tag_rule::sweepX(frame.seconds, style.periodSeconds));
    std::vector<ImVec2> line;
    for (const partner_tag_rule::Point& point : points) line.emplace_back(origin.x + point.x * frame.scale, origin.y + point.y * frame.scale);
    if (line.size() < 2) return;
    frame.list->AddPolyline(line.data(), static_cast<int>(line.size()), hud_text::colour(style.rgb, kOpacity), ImDrawFlags_None, kTraceStroke * frame.scale);
}

}  // namespace

namespace hud {

void drawPartnerTag(const Frame& frame, const partner_status::Tag& tag) {
    if (tag.name.empty()) return;
    const float s = frame.scale;
    const float shadow = hud_text::pixel(s);
    const std::string name = hud_text::upper(tag.name);
    const float nameSize = kNameSize * s;
    const ImVec2 nameExtent = hud_text::measure(Face::CinzelMedium, nameSize, name, kNameSpacing * nameSize);
    const float detailSize = kDetailSize * s;
    const float detailHeight = tag.detail.empty() ? 0.0f : kDetailGap * s + hud_text::measure(Face::CormorantItalic, detailSize, tag.detail, 0.0f).y;
    const ImVec2 at(kLeft * s, frame.display.y - kBottom * s - nameExtent.y - detailHeight);
    hud_text::draw(frame.list, Face::CinzelMedium, nameSize, at, name, kNameSpacing * nameSize, hud_text::colour(kNameRgb, kOpacity), shadow);
    if (tag.hasCondition) {
        drawTrace(frame, ImVec2(at.x + nameExtent.x + kTraceGap * s, at.y + (nameExtent.y - partner_tag_rule::kTraceHeight * s) * 0.5f), tag.condition);
    }
    if (!tag.detail.empty()) {
        hud_text::draw(frame.list, Face::CormorantItalic, detailSize, ImVec2(at.x, at.y + nameExtent.y + kDetailGap * s), tag.detail, 0.0f,
                       hud_text::colour(kDetailRgb, kOpacity), shadow);
    }
}

}  // namespace hud
