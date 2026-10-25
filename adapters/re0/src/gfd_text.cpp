#include "gfd_text.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

#include "gfd_rule.h"
#include "hud_text.h"
#include "imgui_internal.h"
#include "log.h"
#include "paths.h"
#include "ui_art_files.h"
#include "ui_textures.h"

namespace {

constexpr uint32_t kFallbackCode = '?';
constexpr float kPlainSizeRatio = 0.75f;  // the default font against the game font's line height

std::optional<gfd_rule::Font> g_font;  // render thread only, like everything below
bool g_loaded = false;                 // the layout was tried once
const ui_textures::Image* g_atlas = nullptr;

std::optional<gfd_rule::Font> readLayout() {
    const std::wstring path = gameDirectory() + L"\\" + ui_art_files::kArtFolder + L"\\" + ui_art_files::kGameFontLayout;
    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto font = gfd_rule::parse(bytes);
    if (!font) logger::write("gfd_text: %ls is missing or not a font layout, banners use the default font", path.c_str());
    return font;
}

const gfd_rule::Glyph* glyphOf(uint32_t code) {
    if (const gfd_rule::Glyph* glyph = g_font->find(code)) return glyph;
    return g_font->find(kFallbackCode);
}

// The characters of a UTF-8 text.
template <typename Visit>
void forEachCode(std::string_view text, Visit visit) {
    const char* at = text.data();
    const char* const end = at + text.size();
    while (at < end) {
        unsigned int code = 0;
        const int bytes = ImTextCharFromUtf8(&code, at, end);
        at += bytes > 0 ? bytes : 1;
        visit(code);
    }
}

// One copy of the text, glyph by glyph on the pixel grid.
void drawPass(ImDrawList* list, ImVec2 pos, std::string_view text, float scale, ImU32 colour) {
    const ImTextureRef texture(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_atlas->texture)));
    const ImVec2 atlasSize(static_cast<float>(g_atlas->width), static_cast<float>(g_atlas->height));
    float pen = pos.x;
    forEachCode(text, [&](uint32_t code) {
        const gfd_rule::Glyph* glyph = glyphOf(code);
        if (!glyph) return;
        const ImVec2 from(std::round(pen + glyph->xOffset * scale), std::round(pos.y));
        const ImVec2 to(from.x + glyph->width * scale, from.y + glyph->height * scale);
        list->AddImage(texture, from, to, ImVec2(glyph->x / atlasSize.x, glyph->y / atlasSize.y),
                       ImVec2((glyph->x + glyph->width) / atlasSize.x, (glyph->y + glyph->height) / atlasSize.y), colour);
        pen += glyph->advance * scale;
    });
}

}  // namespace

namespace gfd_text {

void prepare(IDirect3DDevice9* device) {
    if (!g_loaded) {
        g_loaded = true;
        g_font = readLayout();
    }
    g_atlas = nullptr;
    if (!g_font) return;
    const std::wstring atlas = std::wstring(ui_art_files::kArtFolder) + L"\\" + ui_art_files::kGameFontImage;
    const ui_textures::Image& image = ui_textures::get(device, atlas);
    if (image.texture) g_atlas = &image;
}

float lineHeight(float scale) { return gfd_rule::kLineHeight * scale; }

ImVec2 measure(std::string_view text, float scale) {
    if (!g_atlas) return hud_text::measure(hud_text::Face::Plain, lineHeight(scale) * kPlainSizeRatio, text, 0.0f);
    float width = 0.0f;
    forEachCode(text, [&](uint32_t code) {
        if (const gfd_rule::Glyph* glyph = glyphOf(code)) width += glyph->advance * scale;
    });
    return ImVec2(width, lineHeight(scale));
}

void draw(ImDrawList* list, ImVec2 pos, std::string_view text, float scale, ImU32 colour, float shadow) {
    if (!g_atlas) {
        const float size = lineHeight(scale) * kPlainSizeRatio;
        hud_text::draw(list, hud_text::Face::Plain, size, ImVec2(pos.x, pos.y + (lineHeight(scale) - size) * 0.5f), text, 0.0f, colour, shadow);
        return;
    }
    if (shadow > 0.0f) drawPass(list, ImVec2(pos.x + shadow, pos.y + shadow), text, scale, IM_COL32(0, 0, 0, colour >> IM_COL32_A_SHIFT));
    drawPass(list, pos, text, scale, colour);
}

}  // namespace gfd_text
