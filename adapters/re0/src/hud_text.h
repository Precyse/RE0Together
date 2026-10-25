#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "imgui.h"

// Text for the in-game HUD (docs/RE0_NOTES.md "Co-op HUD"): the three typefaces shipped in coop\fonts, drawn with a black
// shadow and optional letter spacing, and the helpers every HUD part shares (the scale of the screen, colours with alpha).
// Sizes are given at 720 lines and multiplied by the scale. The render thread only.
namespace hud_text {

constexpr float kReferenceHeight = 720.0f;

enum class Face : uint8_t { CinzelMedium, CinzelBold, CormorantItalic, Plain };  // Plain: the default font

// A screen's scale against the 720 line reference.
constexpr float scaleOf(float displayHeight) { return displayHeight / kReferenceHeight; }

// One reference pixel at `scale`, never less than a whole pixel: the drop shadow's offset and a hairline's thickness.
float pixel(float scale);

// 0xRRGGBB with an opacity 0..1.
ImU32 colour(uint32_t rgb, float alpha);

// Adds the typefaces to the font atlas, after the default font; a missing file leaves its face on the default font (logged
// once). Call once right after the ImGui context exists.
void load();

// The text's size with `spacing` extra pixels after every character.
ImVec2 measure(Face face, float size, std::string_view text, float spacing);

// Distance from the text's top to its baseline.
float ascent(Face face, float size);

// Draws `text` with its top left at `pos`, a black copy `shadow` pixels down and right first (none when 0).
void draw(ImDrawList* list, Face face, float size, ImVec2 pos, std::string_view text, float spacing, ImU32 colour, float shadow);

// The ASCII letters of `text` in capitals.
std::string upper(std::string_view text);

// The text in UTF-8 (ImGui's encoding).
std::string utf8(const std::wstring& text);

}  // namespace hud_text
