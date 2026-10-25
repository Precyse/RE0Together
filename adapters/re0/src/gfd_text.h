#pragma once
#include <d3d9.h>

#include <string_view>

#include "imgui.h"

// Text in the game's own message font (docs/RE0_NOTES.md "The game's own UI art and font"): the glyph rectangles of the
// layout file (gfd_rule.h) cut from the font's atlas picture and drawn one by one. The render thread only.
namespace gfd_text {

// Loads the layout and the atlas on first use and keeps the atlas texture current. While either is missing (logged once)
// `measure` and `draw` use the default font with the same line height.
void prepare(IDirect3DDevice9* device);

// Height of a line at `scale` (1 at 720 lines).
float lineHeight(float scale);

// The size of `text` drawn at `scale`.
ImVec2 measure(std::string_view text, float scale);

// Draws `text` with its top left at `pos`, a black copy `shadow` pixels down and right first (none when 0). `colour` carries
// the alpha.
void draw(ImDrawList* list, ImVec2 pos, std::string_view text, float scale, ImU32 colour, float shadow);

}  // namespace gfd_text
