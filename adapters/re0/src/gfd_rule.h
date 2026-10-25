#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Pure reader of the game's font layout file (GFD, MT Framework type 0x2d462600; docs/RE0_NOTES.md "The game's own UI art
// and font", unit tested): the glyph rectangles in the font's atlas and how far each advances the pen. The launcher copies
// the file out of the player's own install (coop\ui\font_eng.gfd); nothing of the game is kept in the repository.
namespace gfd_rule {

constexpr char kMagic[] = {'G', 'F', 'D', '\0'};
constexpr size_t kCountOffset = 0x1c;
constexpr size_t kPathLengthOffset = 0x34;  // u32, the atlas path's length without its NUL
constexpr size_t kPathOffset = 0x38;
constexpr size_t kGlyphBytes = 16;
constexpr uint32_t kMaxGlyphs = 4096;
constexpr int kLineHeight = 27;  // the English font: every glyph is one line tall

// Fields of a glyph record: {u32 code; u32 position; u32 size; u32 metrics}.
constexpr uint32_t kCoordinateMask = 0xfff;
constexpr int kLowCoordinateShift = 8;    // x in the position word, width in the size word
constexpr int kHighCoordinateShift = 20;  // y in the position word, height in the size word
constexpr uint32_t kByteMask = 0xff;
constexpr int kOffsetShift = 16;          // x offset in the metrics word; the advance is its low byte

struct Glyph {
    uint32_t code = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t advance = 0;
    uint8_t xOffset = 0;
};

struct Font {
    std::string atlasPath;       // "ui\00_font\font_eng"
    std::vector<Glyph> glyphs;   // ordered by code

    const Glyph* find(uint32_t code) const {
        const auto at = std::lower_bound(glyphs.begin(), glyphs.end(), code, [](const Glyph& glyph, uint32_t wanted) { return glyph.code < wanted; });
        return at != glyphs.end() && at->code == code ? &*at : nullptr;
    }
};

inline uint32_t word(std::span<const uint8_t> bytes, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

inline Glyph glyphAt(std::span<const uint8_t> bytes, size_t offset) {
    const uint32_t position = word(bytes, offset + 4);
    const uint32_t size = word(bytes, offset + 8);
    const uint32_t metrics = word(bytes, offset + 12);
    Glyph glyph;
    glyph.code = word(bytes, offset);
    glyph.x = static_cast<uint16_t>((position >> kLowCoordinateShift) & kCoordinateMask);
    glyph.y = static_cast<uint16_t>((position >> kHighCoordinateShift) & kCoordinateMask);
    glyph.width = static_cast<uint16_t>((size >> kLowCoordinateShift) & kCoordinateMask);
    glyph.height = static_cast<uint16_t>((size >> kHighCoordinateShift) & kCoordinateMask);
    glyph.advance = static_cast<uint8_t>(metrics & kByteMask);
    glyph.xOffset = static_cast<uint8_t>((metrics >> kOffsetShift) & kByteMask);
    return glyph;
}

// The layout in `bytes`, or nothing when it is not a GFD, its counts do not fit the file or it holds no glyph.
inline std::optional<Font> parse(std::span<const uint8_t> bytes) {
    if (bytes.size() < kPathOffset || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return std::nullopt;
    const uint32_t count = word(bytes, kCountOffset);
    const uint32_t pathLength = word(bytes, kPathLengthOffset);
    if (count == 0 || count > kMaxGlyphs || pathLength == 0 || pathLength >= bytes.size() - kPathOffset) return std::nullopt;
    const size_t glyphsAt = kPathOffset + pathLength + 1;  // the path's NUL comes first
    if (bytes.size() - glyphsAt < static_cast<size_t>(count) * kGlyphBytes) return std::nullopt;
    Font font;
    font.atlasPath.assign(reinterpret_cast<const char*>(bytes.data()) + kPathOffset, pathLength);
    font.glyphs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) font.glyphs.push_back(glyphAt(bytes, glyphsAt + static_cast<size_t>(i) * kGlyphBytes));
    std::sort(font.glyphs.begin(), font.glyphs.end(), [](const Glyph& a, const Glyph& b) { return a.code < b.code; });
    return font;
}

}  // namespace gfd_rule
