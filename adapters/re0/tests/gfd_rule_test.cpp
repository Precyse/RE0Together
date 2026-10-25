// Checks the font layout reader on a small synthetic buffer: the header, the atlas path, the glyph fields and every refusal
// (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/gfd_rule.h"

namespace {

using namespace gfd_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

constexpr char kAtlas[] = "ui\\00_font\\font_eng";

void put(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); }

// A glyph record from the fields the file packs: x, y, width, height, advance, x offset.
void addGlyph(std::vector<uint8_t>& bytes, uint32_t code, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t advance,
              uint32_t xOffset) {
    const size_t at = bytes.size();
    bytes.resize(at + kGlyphBytes);
    put(bytes, at, code);
    put(bytes, at + 4, x << kLowCoordinateShift | y << kHighCoordinateShift);
    put(bytes, at + 8, width << kLowCoordinateShift | height << kHighCoordinateShift);
    put(bytes, at + 12, advance | xOffset << kOffsetShift);
}

// The header with the count and the path, then the glyphs the caller adds.
std::vector<uint8_t> header(uint32_t count) {
    std::vector<uint8_t> bytes(kPathOffset + sizeof(kAtlas), 0);
    std::memcpy(bytes.data(), kMagic, sizeof(kMagic));
    put(bytes, kCountOffset, count);
    put(bytes, kPathLengthOffset, sizeof(kAtlas) - 1);
    std::memcpy(bytes.data() + kPathOffset, kAtlas, sizeof(kAtlas));
    return bytes;
}

std::vector<uint8_t> sample() {
    std::vector<uint8_t> bytes = header(3);
    addGlyph(bytes, 0x42, 100, 40, 12, 27, 13, 1);  // B, written before A: the reader orders by code
    addGlyph(bytes, 0x20, 14, 0, 6, 27, 6, 4);
    addGlyph(bytes, 0x41, 1000, 229, 14, 27, 15, 0);
    return bytes;
}

void testParse() {
    const std::vector<uint8_t> bytes = sample();
    const auto font = parse(bytes);
    check(font.has_value(), "a well formed file parses");
    if (!font) return;
    check(font->atlasPath == "ui\\00_font\\font_eng", "the atlas path is read without its NUL");
    check(font->glyphs.size() == 3, "every glyph is read");
    check(font->glyphs[0].code == 0x20 && font->glyphs[2].code == 0x42, "the glyphs are ordered by code");
    const Glyph* a = font->find(0x41);
    check(a && a->x == 1000 && a->y == 229 && a->width == 14 && a->height == 27, "the rectangle is bits 8..19 and 20..31");
    check(a && a->advance == 15 && a->xOffset == 0, "the advance is the low byte");
    const Glyph* space = font->find(0x20);
    check(space && space->advance == 6 && space->xOffset == 4, "the x offset is bits 16..23");
    check(!font->find(0x43), "a code the font lacks is not found");
}

void testRefusals() {
    std::vector<uint8_t> bytes = sample();
    check(!parse(std::span<const uint8_t>(bytes.data(), kPathOffset - 1)), "a file shorter than its header is refused");
    std::vector<uint8_t> magic = bytes;
    magic[0] = 'X';
    check(!parse(magic), "another magic is refused");
    check(!parse(std::span<const uint8_t>(bytes.data(), bytes.size() - 1)), "glyphs cut short are refused");
    std::vector<uint8_t> none = header(0);
    check(!parse(none), "no glyph is refused");
    std::vector<uint8_t> many = sample();
    put(many, kCountOffset, kMaxGlyphs + 1);
    check(!parse(many), "a count over the limit is refused");
    std::vector<uint8_t> path = sample();
    put(path, kPathLengthOffset, 0);
    check(!parse(path), "an empty path is refused");
    put(path, kPathLengthOffset, 0x7fffffff);
    check(!parse(path), "a path past the file is refused");
}

}  // namespace

int main() {
    testParse();
    testRefusals();
    if (g_failures == 0) std::printf("gfd_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
