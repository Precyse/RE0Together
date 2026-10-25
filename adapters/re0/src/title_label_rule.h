#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Pure image rules of the Co-op label on the title menu (title_label.cpp, unit tested). The title's row labels are
// cells of one DXT5 atlas (ui\50_title\tex\title_00_<lang>_win, 512 x 512, one level, rows of 44 px); the fifth
// row shows the cell of the console "Online Store" label. The label is rebuilt in memory from letters cut out of the
// atlas's own rows (C from Credits, o and p from Options, the hyphen drawn as a bar sized from those letters), so it
// has the game's lettering in each language; a language without the letters gets text drawn by the system font
// (title_label.cpp) through `outlinedCell`.
namespace title_label_rule {

constexpr uint32_t kAtlasSize = 512;
constexpr uint32_t kRowHeight = 44;   // the atlas's label rows, row r at y = 44 * (r + 1)
constexpr uint32_t kCellWidth = 256;  // the left column of labels
constexpr uint32_t kCellHeight = kRowHeight;
constexpr uint32_t kTargetRow = 4;    // the "Online Store" row: every language's ink lies inside its cell
constexpr uint32_t kInkLeft = 6;      // where the game's labels start their ink (outline included) inside a cell
constexpr uint32_t kInkTop = 6;
constexpr uint32_t kTextOutline = 2;   // system-font fallback outline
constexpr uint32_t kGlyphOutline = 3;  // the atlas's outline around a letter's fill
constexpr uint8_t kFillLevel = 160;    // a fill texel is white and opaque above this (the outline is black)

constexpr uint32_t kBlockSize = 4;
constexpr size_t kBlockBytes = 16;  // DXT5: 8 bytes alpha, 8 bytes color
constexpr uint32_t kBlocksWide = kCellWidth / kBlockSize;
constexpr uint32_t kBlocksHigh = kCellHeight / kBlockSize;
constexpr size_t kCellBytes = size_t{kBlocksWide} * kBlocksHigh * kBlockBytes;
static_assert(kRowHeight % kBlockSize == 0 && kCellWidth % kBlockSize == 0);
constexpr uint32_t rowTop(uint32_t row) { return kRowHeight * (row + 1); }
static_assert(rowTop(kTargetRow) + kCellHeight <= kAtlasSize);

// One texel of a label: white over black, so a gray level and an alpha are enough.
struct Texel {
    uint8_t gray;
    uint8_t alpha;
};
using Cell = std::vector<Texel>;  // kCellWidth x kCellHeight, row-major

inline Texel& at(Cell& cell, uint32_t x, uint32_t y) { return cell[size_t(y) * kCellWidth + x]; }
inline Texel at(const Cell& cell, uint32_t x, uint32_t y) { return cell[size_t(y) * kCellWidth + x]; }
inline Cell emptyCell() { return Cell(size_t{kCellWidth} * kCellHeight, Texel{0, 0}); }
constexpr bool isFill(Texel t) { return t.alpha > kFillLevel && t.gray > kFillLevel; }

// ---- DXT5 ----

// The eight alpha levels of a DXT5 alpha block (a0 > a1: interpolated; else six levels plus 0 and 255).
constexpr std::array<uint32_t, 8> alphaLevels(uint32_t a0, uint32_t a1) {
    std::array<uint32_t, 8> levels{a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1) {
        for (uint32_t i = 1; i < 7; ++i) levels[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (uint32_t i = 1; i < 5; ++i) levels[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        levels[7] = 255;
    }
    return levels;
}

// Gray levels of a DXT color block from the 6-bit green channel (the labels are gray: red and blue follow it).
constexpr uint32_t expand6(uint32_t g6) { return (g6 << 2) | (g6 >> 4); }
constexpr std::array<uint32_t, 4> grayLevels(uint16_t c0, uint16_t c1) {
    const uint32_t v0 = expand6((c0 >> 5) & 0x3f), v1 = expand6((c1 >> 5) & 0x3f);
    if (c0 > c1) return {v0, v1, (2 * v0 + v1) / 3, (v0 + 2 * v1) / 3};
    return {v0, v1, (v0 + v1) / 2, 0};
}
constexpr uint16_t gray565(uint32_t g6) { return uint16_t(((g6 >> 1) << 11) | (g6 << 5) | (g6 >> 1)); }

template <size_t N>
constexpr uint32_t nearest(const std::array<uint32_t, N>& levels, uint32_t value) {
    const auto distance = [value](uint32_t level) { return level > value ? level - value : value - level; };
    uint32_t best = 0;
    for (uint32_t i = 1; i < N; ++i) {
        if (distance(levels[i]) < distance(levels[best])) best = i;
    }
    return best;
}

inline std::array<Texel, 16> decodeBlock(const uint8_t* b) {
    const auto alpha = alphaLevels(b[0], b[1]);
    uint64_t alphaBits = 0;
    for (uint32_t i = 0; i < 6; ++i) alphaBits |= uint64_t(b[2 + i]) << (8 * i);
    const auto gray = grayLevels(uint16_t(b[8] | (b[9] << 8)), uint16_t(b[10] | (b[11] << 8)));
    const uint32_t colorBits = b[12] | (b[13] << 8) | (b[14] << 16) | (uint32_t(b[15]) << 24);
    std::array<Texel, 16> out{};
    for (uint32_t i = 0; i < 16; ++i) {
        out[i] = Texel{uint8_t(gray[(colorBits >> (2 * i)) & 3]), uint8_t(alpha[(alphaBits >> (3 * i)) & 7])};
    }
    return out;
}

// One 4 x 4 block (row-major texels) as DXT5: interpolated alpha between the block's extremes, and a gray ramp between
// the brightest and the darkest visible texel (invisible texels take any color).
inline std::array<uint8_t, kBlockBytes> encodeBlock(const std::array<Texel, 16>& texels) {
    std::array<uint8_t, kBlockBytes> out{};
    uint32_t aMax = 0, aMin = 255, gMax = 0, gMin = 63;
    for (const Texel& t : texels) {
        aMax = std::max<uint32_t>(aMax, t.alpha);
        aMin = std::min<uint32_t>(aMin, t.alpha);
        if (!t.alpha) continue;
        gMax = std::max<uint32_t>(gMax, t.gray >> 2);
        gMin = std::min<uint32_t>(gMin, t.gray >> 2);
    }
    if (gMin > gMax) gMin = gMax;  // no visible texel

    out[0] = uint8_t(aMax);
    out[1] = uint8_t(aMin);
    uint64_t alphaBits = 0;
    if (aMax > aMin) {
        const auto levels = alphaLevels(aMax, aMin);
        for (uint32_t i = 0; i < 16; ++i) alphaBits |= uint64_t(nearest(levels, texels[i].alpha)) << (3 * i);
    }
    for (uint32_t i = 0; i < 6; ++i) out[2 + i] = uint8_t(alphaBits >> (8 * i));

    // c0 > c1 keeps the four-color mode; equal endpoints use index 0 everywhere.
    const uint16_t c0 = gray565(gMax), c1 = gray565(gMin);
    out[8] = uint8_t(c0);
    out[9] = uint8_t(c0 >> 8);
    out[10] = uint8_t(c1);
    out[11] = uint8_t(c1 >> 8);
    uint32_t colorBits = 0;
    if (c0 > c1) {
        const auto levels = grayLevels(c0, c1);
        for (uint32_t i = 0; i < 16; ++i) colorBits |= nearest(levels, texels[i].gray) << (2 * i);
    }
    for (uint32_t i = 0; i < 4; ++i) out[12 + i] = uint8_t(colorBits >> (8 * i));
    return out;
}

// The left cell of atlas row `row`, decoded from the level's blocks (`pitch` bytes per block row).
inline Cell decodeCell(const uint8_t* level, size_t pitch, uint32_t row) {
    Cell cell = emptyCell();
    for (uint32_t by = 0; by < kBlocksHigh; ++by) {
        const uint8_t* blockRow = level + (rowTop(row) / kBlockSize + by) * pitch;
        for (uint32_t bx = 0; bx < kBlocksWide; ++bx) {
            const auto texels = decodeBlock(blockRow + bx * kBlockBytes);
            for (uint32_t i = 0; i < 16; ++i) {
                at(cell, bx * kBlockSize + i % kBlockSize, by * kBlockSize + i / kBlockSize) = texels[i];
            }
        }
    }
    return cell;
}

// The cell as DXT5 blocks, block rows top to bottom, kBlocksWide blocks each (one block row per locked pitch).
inline std::vector<uint8_t> encodeCell(const Cell& cell) {
    std::vector<uint8_t> out;
    out.reserve(kCellBytes);
    for (uint32_t by = 0; by < kBlocksHigh; ++by) {
        for (uint32_t bx = 0; bx < kBlocksWide; ++bx) {
            std::array<Texel, 16> block{};
            for (uint32_t i = 0; i < 16; ++i) {
                block[i] = at(cell, bx * kBlockSize + i % kBlockSize, by * kBlockSize + i / kBlockSize);
            }
            const auto bytes = encodeBlock(block);
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
    }
    return out;
}

// ---- letters cut from the atlas ----

struct Run {
    uint32_t begin;  // first fill column
    uint32_t end;    // one past the last
};

// The letters of a cell, left to right: column runs that hold fill texels (the outlines of neighbours touch, the
// fills never do).
inline std::vector<Run> letterRuns(const Cell& cell) {
    std::vector<Run> runs;
    bool inRun = false;
    for (uint32_t x = 0; x <= kCellWidth; ++x) {
        bool fill = false;
        for (uint32_t y = 0; x < kCellWidth && y < kCellHeight && !fill; ++y) fill = isFill(at(cell, x, y));
        if (fill && !inRun) runs.push_back(Run{x, x});
        if (!fill && inRun) runs.back().end = x;
        inRun = fill;
    }
    return runs;
}

// A letter cut out of its cell: its fill and every texel within kGlyphOutline of it, at the cell's own rows.
struct Glyph {
    Cell texels;      // cell-sized, clear outside the letter
    float center;     // middle of the fill columns
    uint32_t top;     // first fill row
    uint32_t bottom;  // last fill row
    uint32_t width;   // fill columns
    uint32_t stroke;  // fill texels across the left side on the middle row
};

inline Glyph cutGlyph(const Cell& cell, Run run) {
    Glyph glyph{emptyCell(), (run.begin + run.end - 1) / 2.0f, kCellHeight, 0, run.end - run.begin, 0};
    for (uint32_t y = 0; y < kCellHeight; ++y) {
        for (uint32_t x = run.begin; x < run.end; ++x) {
            if (!isFill(at(cell, x, y))) continue;
            glyph.top = std::min(glyph.top, y);
            glyph.bottom = std::max(glyph.bottom, y);
        }
    }
    const int r = int(kGlyphOutline);
    const auto nearFill = [&](int x, int y) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int fx = x + dx, fy = y + dy;
                if (fx >= int(run.begin) && fx < int(run.end) && fy >= 0 && fy < int(kCellHeight) &&
                    isFill(at(cell, uint32_t(fx), uint32_t(fy)))) {
                    return true;
                }
            }
        }
        return false;
    };
    const uint32_t left = run.begin > kGlyphOutline ? run.begin - kGlyphOutline : 0;
    const uint32_t right = std::min(kCellWidth, run.end + kGlyphOutline);
    for (uint32_t y = 0; y < kCellHeight; ++y) {
        for (uint32_t x = left; x < right; ++x) {
            if (nearFill(int(x), int(y))) at(glyph.texels, x, y) = at(cell, x, y);
        }
    }
    const uint32_t middle = (glyph.top + glyph.bottom) / 2;
    for (uint32_t x = run.begin; x < (run.begin + run.end) / 2; ++x) glyph.stroke += isFill(at(cell, x, middle));
    return glyph;
}

// Draws `from` into `to` shifted by dx; where both have ink the more opaque (then the whiter) texel wins, as the
// outlines of neighbouring letters overlap in the game's own cells.
inline void stamp(Cell& to, const Cell& from, int dx) {
    for (uint32_t y = 0; y < kCellHeight; ++y) {
        for (uint32_t x = 0; x < kCellWidth; ++x) {
            const Texel t = at(from, x, y);
            const int tx = int(x) + dx;
            if (!t.alpha || tx < 0 || tx >= int(kCellWidth)) continue;
            Texel& o = at(to, uint32_t(tx), y);
            if (t.alpha > o.alpha || (t.alpha == o.alpha && t.gray > o.gray)) o = t;
        }
    }
}

// A coverage mask (0..255 per pixel, row-major) of `width` x `height`.
struct Mask {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> coverage;

    uint8_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height)) return 0;
        return coverage[size_t(y) * width + x];
    }
};

// The coverage with a black outline of `radius` around it, composited white over black, cell-sized, with the
// ink's top-left at (inkLeft, inkTop). Ink that does not fit the cell is cut.
inline Cell outlinedCell(const Mask& text, uint32_t radius, uint32_t inkLeft, uint32_t inkTop) {
    int minX = int(text.width), minY = int(text.height), maxX = -1;
    for (uint32_t y = 0; y < text.height; ++y) {
        for (uint32_t x = 0; x < text.width; ++x) {
            if (!text.at(int(x), int(y))) continue;
            minX = std::min(minX, int(x));
            minY = std::min(minY, int(y));
            maxX = std::max(maxX, int(x));
        }
    }
    Cell cell = emptyCell();
    if (maxX < 0) return cell;
    const int r = int(radius);
    const int shiftX = int(inkLeft) - (minX - r);
    const int shiftY = int(inkTop) - (minY - r);
    for (uint32_t cy = 0; cy < kCellHeight; ++cy) {
        for (uint32_t cx = 0; cx < kCellWidth; ++cx) {
            const int x = int(cx) - shiftX;
            const int y = int(cy) - shiftY;
            uint8_t outline = 0;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy <= r * r) outline = std::max(outline, text.at(x + dx, y + dy));
                }
            }
            const uint32_t fill = text.at(x, y);
            // White fill over the black outline: alpha = fill + outline * (1 - fill), color = fill / alpha.
            const uint32_t alpha = fill + outline * (255 - fill) / 255;
            const uint32_t gray = alpha ? std::min<uint32_t>(255, fill * 255 / alpha) : 0;
            at(cell, cx, cy) = Texel{uint8_t(gray), uint8_t(alpha)};
        }
    }
    return cell;
}

// ---- the label per language ----

// Where a letter is taken from: the atlas row, its index among the row's letters, and how many letters that row must
// hold (anything else is not the label the recipe was made from).
struct Source {
    uint32_t row;
    uint32_t index;
    uint32_t letters;
};

// "Co-op" in the case of the language's labels: C, o, hyphen, o, p; the pitch is the distance from the Options
// row's first letter to its p (the same "Op" pair in every language that has one).
struct Recipe {
    std::string_view language;  // the atlas's language code (title_00_<code>_win)
    Source c;
    Source o;
    Source p;
};

constexpr uint32_t kCreditsRow = 7;
constexpr uint32_t kOptionsRow = 3;
constexpr uint32_t kLoadRow = 1;
// German has no capital C in its labels: no recipe, the fallback draws the label.
constexpr std::array kRecipes = std::to_array<Recipe>({
    {"eng", {kCreditsRow, 0, 7}, {kOptionsRow, 4, 7}, {kOptionsRow, 1, 7}},  // Credits, Options
    {"chS", {kCreditsRow, 0, 7}, {kOptionsRow, 4, 7}, {kOptionsRow, 1, 7}},  // the English atlas
    {"chT", {kCreditsRow, 0, 7}, {kOptionsRow, 4, 7}, {kOptionsRow, 1, 7}},
    {"fre", {kCreditsRow, 0, 7}, {kOptionsRow, 4, 7}, {kOptionsRow, 1, 7}},  // Credits, Options
    {"spa", {kCreditsRow, 0, 8}, {kOptionsRow, 4, 8}, {kOptionsRow, 1, 8}},  // Creditos, Opciones
    {"ita", {kLoadRow, 0, 13}, {kOptionsRow, 4, 7}, {kOptionsRow, 1, 7}},    // Carica partita, Opzioni
    {"jpn", {kCreditsRow, 0, 6}, {kOptionsRow, 0, 6}, {kOptionsRow, 1, 6}},  // CREDIT, OPTION: "CO-OP"
});

inline const Recipe* recipeFor(std::string_view language) {
    for (const Recipe& recipe : kRecipes) {
        if (recipe.language == language) return &recipe;
    }
    return nullptr;
}

// The language code in the atlas path ("...title_00_eng_win" -> "eng"), empty when the path is not that shape.
inline std::string_view languageOfPath(std::string_view path) {
    constexpr std::string_view kPrefix = "title_00_";
    constexpr size_t kCodeLength = 3;
    const size_t at = path.rfind(kPrefix);
    if (at == std::string_view::npos || at + kPrefix.size() + kCodeLength > path.size()) return {};
    return path.substr(at + kPrefix.size(), kCodeLength);
}

// The hyphen: a white bar with the letters' outline, as wide as the o's fill plus one, one texel thicker than the o's
// stroke, centred between the C's top and the o's baseline (where the game's own hyphen sits).
inline Cell hyphen(const Glyph& c, const Glyph& o, float center) {
    const uint32_t thickness = o.stroke + 1;
    const uint32_t width = o.width + 1;
    const float middle = (c.top + o.bottom) / 2.0f;
    Mask bar{kCellWidth, kCellHeight, std::vector<uint8_t>(size_t{kCellWidth} * kCellHeight, 0)};
    const int left = int(center - width / 2.0f + 0.5f), top = int(middle - thickness / 2.0f + 0.5f);
    for (int y = top; y < top + int(thickness); ++y) {
        for (int x = left; x < left + int(width); ++x) {
            if (x < 0 || y < 0 || x >= int(kCellWidth) || y >= int(kCellHeight)) continue;
            bar.coverage[size_t(y) * kCellWidth + x] = 255;
        }
    }
    return outlinedCell(bar, kGlyphOutline, uint32_t(std::max(0, left - int(kGlyphOutline))),
                        uint32_t(std::max(0, top - int(kGlyphOutline))));
}

// The label built from the atlas's own letters; `rowCell(row)` decodes an atlas row. Empty when a source row does not
// hold the letters the recipe expects.
template <class RowCell>
std::optional<Cell> composeLabel(const Recipe& recipe, RowCell rowCell) {
    const Cell cRow = rowCell(recipe.c.row), oRow = rowCell(recipe.o.row), pRow = rowCell(recipe.p.row);
    const auto cRuns = letterRuns(cRow), oRuns = letterRuns(oRow), pRuns = letterRuns(pRow);
    if (cRuns.size() != recipe.c.letters || oRuns.size() != recipe.o.letters || pRuns.size() != recipe.p.letters) {
        return std::nullopt;
    }
    const Glyph c = cutGlyph(cRow, cRuns[recipe.c.index]);
    const Glyph o = cutGlyph(oRow, oRuns[recipe.o.index]);
    const Glyph p = cutGlyph(pRow, pRuns[recipe.p.index]);
    const float pitch = p.center - cutGlyph(pRow, pRuns[0]).center;
    const auto shift = [](const Glyph& g, float center) {
        const float dx = center - g.center;
        return int(dx + (dx >= 0 ? 0.5f : -0.5f));
    };

    Cell label = emptyCell();
    const float start = c.center;  // the first letter stays where the game's labels start
    stamp(label, c.texels, shift(c, start));
    stamp(label, o.texels, shift(o, start + pitch));
    stamp(label, hyphen(c, o, start + 2 * pitch), 0);
    stamp(label, o.texels, shift(o, start + 3 * pitch));
    stamp(label, p.texels, shift(p, start + 4 * pitch));
    return label;
}

}  // namespace title_label_rule
