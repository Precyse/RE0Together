// Checks the Co-op label rules of the title menu: the cell, DXT5 encode and decode, cutting letters out of a row, the
// composed label, the recipes and the system-font fallback's placement, with no game. Exit 0 when every check passes.
#include <array>
#include <cstdio>
#include <vector>

#include "../src/title_label_rule.h"

namespace {

using namespace title_label_rule;

// The "Online Store" ink measured in the eight language atlases of title_us.arc / title_eu.arc (outline included).
constexpr uint32_t kStoreInkRight = 243;     // exclusive, French
constexpr uint32_t kStoreInkTop = 226;
constexpr uint32_t kStoreInkBottom = 264;    // exclusive, Italian
constexpr uint32_t kOptionsInkBottom = 220;  // the row above ends here (its descenders)
constexpr uint32_t kGalleryInkTop = 268;     // the row below starts here (Spanish)

constexpr uint32_t kAlphaTolerance = 255 / 14 + 1;  // half a step of the eight-level ramp
constexpr uint32_t kGrayTolerance = 255 / 6 + 4;    // half a step of the four-level ramp, plus 565 rounding

// Synthetic letters: a white bar with a 3 px black outline, one every kPitch columns, so neighbouring outlines touch
// as in the game's cells.
constexpr uint32_t kFirstFill = 9;
constexpr uint32_t kPitch = 12;
constexpr uint32_t kLetterWidth = 8;
constexpr uint32_t kLetterTop = 10;
constexpr uint32_t kLetterBottom = 34;  // exclusive

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

uint32_t gap(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

Cell rowOfLetters(uint32_t letters) {
    Cell cell = emptyCell();
    for (uint32_t i = 0; i < letters; ++i) {
        const uint32_t left = kFirstFill + i * kPitch;
        for (uint32_t y = kLetterTop - kGlyphOutline; y < kLetterBottom + kGlyphOutline; ++y) {
            for (uint32_t x = left - kGlyphOutline; x < left + kLetterWidth + kGlyphOutline; ++x) {
                const bool fill = y >= kLetterTop && y < kLetterBottom && x >= left && x < left + kLetterWidth;
                Texel& t = at(cell, x, y);
                if (fill || t.alpha == 0) t = fill ? Texel{255, 255} : Texel{0, 255};
            }
        }
    }
    return cell;
}

void testCell() {
    const uint32_t top = rowTop(kTargetRow);
    check(kInkLeft == 6 && kCellWidth >= kStoreInkRight, "the cell spans every label's width");
    check(top <= kStoreInkTop && top + kCellHeight >= kStoreInkBottom, "the cell spans every label's height");
    check(top >= kOptionsInkBottom && top + kCellHeight <= kGalleryInkTop, "the rows above and below are left alone");
    check(top + kInkTop == kStoreInkTop, "ink starts where the game's does");
    check(rowTop(kOptionsRow) == 176 && rowTop(kCreditsRow) == 352 && rowTop(kLoadRow) == 88, "source rows");
    check(kCellBytes == size_t{64} * 11 * 16, "64 x 11 blocks of 16 bytes");
}

void testEncode() {
    std::array<Texel, 16> clear{};
    bool allClear = true;
    for (const Texel& t : decodeBlock(encodeBlock(clear).data())) allClear = allClear && t.alpha == 0;
    check(allClear, "a transparent block stays transparent");

    std::array<Texel, 16> white{};
    white.fill(Texel{255, 255});
    bool allWhite = true;
    for (const Texel& t : decodeBlock(encodeBlock(white).data())) allWhite = allWhite && t.alpha == 255 && t.gray == 255;
    check(allWhite, "an opaque white block stays exact");

    std::array<Texel, 16> block{};
    for (uint32_t i = 0; i < 16; ++i) block[i] = Texel{uint8_t(i * 17), uint8_t(255 - i * 9)};
    block[3] = Texel{0, 0};
    const auto bytes = encodeBlock(block);
    const auto decoded = decodeBlock(bytes.data());
    bool alphaOk = true, grayOk = true;
    for (uint32_t i = 0; i < 16; ++i) {
        alphaOk = alphaOk && gap(decoded[i].alpha, block[i].alpha) <= kAlphaTolerance;
        if (block[i].alpha) grayOk = grayOk && gap(decoded[i].gray, block[i].gray) <= kGrayTolerance;
    }
    check(alphaOk, "alpha decodes within half a ramp step");
    check(grayOk, "gray of visible texels decodes within half a ramp step");
    check(decoded[3].alpha == 0, "a transparent texel stays transparent");
    check((bytes[8] | (bytes[9] << 8)) > (bytes[10] | (bytes[11] << 8)), "the color block stays in four-color mode");

    const auto sixLevel = alphaLevels(10, 200);
    check(sixLevel[6] == 0 && sixLevel[7] == 255, "a0 <= a1 has the 0 and 255 levels (the game's own blocks)");
}

void testCellRoundTrip() {
    // A cell written into a level at the target row decodes back from the same place.
    const Cell row = rowOfLetters(3);
    const std::vector<uint8_t> blocks = encodeCell(row);
    constexpr size_t kPitchBytes = size_t{kAtlasSize / kBlockSize} * kBlockBytes;
    std::vector<uint8_t> level(kPitchBytes * (kAtlasSize / kBlockSize), 0);
    const size_t rowBytes = size_t{kBlocksWide} * kBlockBytes;
    for (uint32_t by = 0; by < kBlocksHigh; ++by) {
        const size_t to = (rowTop(kTargetRow) / kBlockSize + by) * kPitchBytes;
        std::copy_n(blocks.begin() + by * rowBytes, rowBytes, level.begin() + to);
    }
    const Cell back = decodeCell(level.data(), kPitchBytes, kTargetRow);
    bool same = true;
    for (size_t i = 0; i < row.size(); ++i) same = same && back[i].alpha == row[i].alpha && back[i].gray == row[i].gray;
    check(same, "a two-level cell survives encode, placement and decode exactly");
}

void testLetters() {
    const Cell row = rowOfLetters(4);
    const auto runs = letterRuns(row);
    check(runs.size() == 4, "touching outlines still give one run per letter");
    check(runs.size() == 4 && runs[1].begin == kFirstFill + kPitch && runs[1].end == kFirstFill + kPitch + kLetterWidth,
          "a run is the letter's fill columns");

    const Glyph g = cutGlyph(row, runs[1]);
    check(g.top == kLetterTop && g.bottom == kLetterBottom - 1 && g.width == kLetterWidth, "the fill box");
    check(g.stroke == kLetterWidth / 2, "the stroke across the left half");
    const uint32_t left = kFirstFill + kPitch;
    check(at(g.texels, left - kGlyphOutline, 20).alpha == 255, "the letter keeps its own outline");
    check(at(row, left - kPitch + kLetterWidth - 1, 20).gray == 255 &&
              at(g.texels, left - kPitch + kLetterWidth - 1, 20).alpha == 0,
          "the left neighbour's fill is not cut with it");
    check(at(g.texels, left + kPitch, 20).alpha == 0, "nor the right neighbour's");
}

void testCompose() {
    // Sources like English: C is letter 0 of a 7-letter row, o letter 4 and p letter 1 of another 7-letter row.
    const Recipe recipe{"eng", {kCreditsRow, 0, 7}, {kOptionsRow, 4, 7}, {kOptionsRow, 1, 7}};
    const auto rowCell = [](uint32_t) { return rowOfLetters(7); };
    const std::optional<Cell> label = composeLabel(recipe, rowCell);
    check(label.has_value(), "the label is built");
    if (label) {
        const auto runs = letterRuns(*label);
        check(runs.size() == 5, "five marks: C, o, hyphen, o, p");
        check(runs.size() == 5 && runs[0].begin == kFirstFill, "the first letter stays where the game's labels start");
        bool evenPitch = runs.size() == 5;
        for (size_t i = 1; evenPitch && i < runs.size(); ++i) {
            const uint32_t centre = runs[i].begin + runs[i].end, before = runs[i - 1].begin + runs[i - 1].end;
            evenPitch = gap(centre - before, 2 * kPitch) <= 1;
        }
        check(evenPitch, "marks one pitch apart (the Op distance of the source)");
        bool hyphenThin = false;
        if (runs.size() == 5) {
            const Glyph bar = cutGlyph(*label, runs[2]);
            hyphenThin = bar.bottom - bar.top + 1 == kLetterWidth / 2 + 1 && bar.width == kLetterWidth + 1;
        }
        check(hyphenThin, "the hyphen is the o's stroke plus one thick and its width plus one wide");
    }

    // The guard: a source row that does not hold the expected letters gives no label (the fallback draws it).
    const auto shortRow = [](uint32_t row) { return rowOfLetters(row == kCreditsRow ? 6 : 7); };
    check(!composeLabel(recipe, shortRow).has_value(), "a row with other letters gives no label");
}

void testRecipes() {
    check(languageOfPath("ui\\50_title\\tex\\title_00_eng_win") == "eng", "language from the atlas path");
    check(languageOfPath("ui\\50_title\\tex\\title_00_jpn_win") == "jpn", "Japanese");
    check(languageOfPath("ui\\50_title\\title").empty() && languageOfPath("title_00_e").empty(), "other paths");
    for (const char* language : {"eng", "fre", "spa", "ita", "jpn", "chS", "chT"}) {
        check(recipeFor(language) != nullptr, "every language with the letters has a recipe");
    }
    check(recipeFor("ger") == nullptr, "German has no C: the fallback draws it");
    bool sameP = true;
    for (const Recipe& r : kRecipes) sameP = sameP && r.p.row == kOptionsRow && r.p.letters == r.o.letters;
    check(sameP, "p and o come from the Options row");
}

void testFallbackPlacement() {
    Mask text{20, 20, std::vector<uint8_t>(400, 0)};
    for (uint32_t y = 8; y < 11; ++y) {
        for (uint32_t x = 5; x < 8; ++x) text.coverage[y * 20 + x] = 255;
    }
    const Cell cell = outlinedCell(text, kTextOutline, kInkLeft, kInkTop);
    const uint32_t r = kTextOutline;
    check(at(cell, kInkLeft + r + 1, kInkTop + r + 1).gray == 255, "the text is white");
    check(at(cell, kInkLeft + r + 1, kInkTop).alpha == 255 && at(cell, kInkLeft + r + 1, kInkTop).gray == 0,
          "the outline's top edge is black at the ink top");
    check(at(cell, kInkLeft - 1, kInkTop + r + 1).alpha == 0, "nothing before the ink start");

    Mask blank{8, 8, std::vector<uint8_t>(64, 0)};
    bool clear = true;
    for (const Texel& t : outlinedCell(blank, kTextOutline, kInkLeft, kInkTop)) clear = clear && t.alpha == 0;
    check(clear, "no text gives a clear cell (the old label is erased)");
}

}  // namespace

int main() {
    testCell();
    testEncode();
    testCellRoundTrip();
    testLetters();
    testCompose();
    testRecipes();
    testFallbackPlacement();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
