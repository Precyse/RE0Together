#include "title_label.h"

#include <windows.h>
#include <d3d9.h>

#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "game.h"
#include "log.h"
#include "title_label_rule.h"

namespace {

using title_label_rule::Cell;
using title_label_rule::Mask;

// The fallback for a language whose labels lack the letters: the system font, condensed and monospace like the
// game's lettering; Windows maps the face to another one when it is missing.
constexpr wchar_t kLabel[] = L"Co-op";
constexpr wchar_t kFontFace[] = L"MS Gothic";
constexpr int kFontEm = 36;     // px, about the game's cap height plus descender
constexpr int kFontWidth = 14;  // px average character width
constexpr int kCanvasWidth = 320;
constexpr int kCanvasHeight = 64;
constexpr int kCanvasMargin = 8;  // room for the outline around the text
constexpr uint32_t kHeightMask = 0x3fff;
constexpr uint32_t kSize512 = title_label_rule::kAtlasSize | (title_label_rule::kAtlasSize << 16);
constexpr uint8_t kOneLevel = 1;

struct Pending {
    IDirect3DTexture9* texture = nullptr;  // holds one reference until applied
    std::string language;                  // the atlas's language code
};

std::mutex g_mutex;
Pending g_pending;  // guarded by g_mutex

// The label's coverage, rendered with GDI (white text on black, grayscale antialiasing).
Mask renderText() {
    Mask mask{kCanvasWidth, kCanvasHeight, std::vector<uint8_t>(size_t{kCanvasWidth} * kCanvasHeight, 0)};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = kCanvasWidth;
    info.bmiHeader.biHeight = -kCanvasHeight;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    HFONT font = CreateFontW(-kFontEm, kFontWidth, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, kFontFace);
    if (bitmap && font) {
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, OPAQUE);
        TextOutW(dc, kCanvasMargin, kCanvasMargin, kLabel, static_cast<int>(wcslen(kLabel)));
        GdiFlush();
        const auto* pixels = static_cast<const uint8_t*>(bits);
        for (size_t i = 0; i < mask.coverage.size(); ++i) mask.coverage[i] = pixels[i * 4 + 1];  // green of BGRA
        SelectObject(dc, oldFont);
        SelectObject(dc, oldBitmap);
    }
    if (font) DeleteObject(font);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    return mask;
}

// The label from the atlas's own letters, or the system font when this language has none to cut.
Cell labelFor(const std::string& language, const uint8_t* level, size_t pitch) {
    const title_label_rule::Recipe* recipe = title_label_rule::recipeFor(language);
    if (recipe) {
        const auto rowCell = [&](uint32_t row) { return title_label_rule::decodeCell(level, pitch, row); };
        if (std::optional<Cell> label = title_label_rule::composeLabel(*recipe, rowCell)) return *label;
        logger::write("title_label: the %s atlas rows are not the expected labels, Co-op drawn with the system font",
                      language.c_str());
    } else {
        logger::write("title_label: the %s atlas has no letters for Co-op, drawn with the system font",
                      language.empty() ? "unknown" : language.c_str());
    }
    return title_label_rule::outlinedCell(renderText(), title_label_rule::kTextOutline, title_label_rule::kInkLeft,
                                          title_label_rule::kInkTop);
}

// The requested resource at `index` of a uGameGUI (its request list, as uGameGUI::getRequestedResource walks it).
uintptr_t requestedResource(uintptr_t gui, uint32_t index) {
    uintptr_t node = game::readPointer(gui + game::kTitleRequestsOffset);
    for (uint32_t i = 0; node && i < index; ++i) node = game::readPointer(node + game::kRequestNextOffset);
    return node ? game::readPointer(node + game::kRequestResourceOffset) : 0;
}

// The D3D texture of the title's atlas when the engine's texture object is the one the cell rules describe.
IDirect3DTexture9* atlasTexture(uintptr_t title) {
    const uintptr_t resource = requestedResource(title, game::kTitleAtlasRequest);
    const uintptr_t texture = resource ? game::readPointer(resource + game::kTextureDrawOffset) : 0;
    uint32_t type = 1, size = 0, format = 0;
    uint8_t levels = 0;
    if (!texture || game::readPointer(texture) != game::kDrawTextureVtable ||
        !game::readMemory(texture + game::kDrawTextureTypeOffset, type) ||
        !game::readMemory(texture + game::kDrawTextureSizeOffset, size) ||
        !game::readMemory(texture + game::kDrawTextureLevelsOffset, levels) ||
        !game::readMemory(texture + game::kDrawTextureFormatOffset, format)) {
        return nullptr;
    }
    const bool expected = type == 0 && (size & (0xffff | (kHeightMask << 16))) == kSize512 && levels == kOneLevel &&
                          format == D3DFMT_DXT5;
    return expected ? reinterpret_cast<IDirect3DTexture9*>(game::readPointer(texture + game::kDrawTextureD3dOffset))
                    : nullptr;
}

std::string atlasLanguage(uintptr_t title) {
    std::array<char, game::kTitleAtlasPathSize> path{};
    if (!game::readMemory(title + game::kTitleAtlasPathOffset, path)) return {};
    path.back() = '\0';
    return std::string(title_label_rule::languageOfPath(path.data()));
}

// The texture itself agrees: level 0 is 512 x 512 DXT5 in the managed pool and there is no other level.
bool lockable(IDirect3DTexture9* texture) {
    D3DSURFACE_DESC desc{};
    return texture->GetLevelCount() == kOneLevel && SUCCEEDED(texture->GetLevelDesc(0, &desc)) &&
           desc.Format == D3DFMT_DXT5 && desc.Pool == D3DPOOL_MANAGED &&
           desc.Width == title_label_rule::kAtlasSize && desc.Height == title_label_rule::kAtlasSize;
}

// Reads the source rows and writes the fifth row's cell under one lock of the level.
bool drawLabel(const Pending& pending) {
    if (!lockable(pending.texture)) return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(pending.texture->LockRect(0, &locked, nullptr, 0))) return false;
    auto* level = static_cast<uint8_t*>(locked.pBits);
    const size_t pitch = size_t(locked.Pitch);
    const std::vector<uint8_t> blocks = title_label_rule::encodeCell(labelFor(pending.language, level, pitch));
    constexpr size_t kRowBytes = size_t{title_label_rule::kBlocksWide} * title_label_rule::kBlockBytes;
    const size_t firstBlockRow = title_label_rule::rowTop(title_label_rule::kTargetRow) / title_label_rule::kBlockSize;
    for (uint32_t row = 0; row < title_label_rule::kBlocksHigh; ++row) {
        std::memcpy(level + (firstBlockRow + row) * pitch, blocks.data() + row * kRowBytes, kRowBytes);
    }
    pending.texture->UnlockRect(0);
    return true;
}

bool guardedDraw(const Pending& pending) {
    __try {
        return drawLabel(pending);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logger::write("title_label: exception 0x%08lx while drawing the label", GetExceptionCode());
        return false;
    }
}

}  // namespace

namespace title_label {

void publish(uintptr_t title) {
    IDirect3DTexture9* texture = atlasTexture(title);
    if (!texture) {
        logger::write("title_label: the title atlas is not the expected texture, the row keeps its label");
        return;
    }
    texture->AddRef();  // kept until the render thread has drawn into it
    Pending next{texture, atlasLanguage(title)};
    std::lock_guard lock(g_mutex);
    if (g_pending.texture) g_pending.texture->Release();
    g_pending = std::move(next);
}

void applyPending() {
    Pending pending;
    {
        std::lock_guard lock(g_mutex);
        std::swap(pending, g_pending);
    }
    if (!pending.texture) return;
    if (guardedDraw(pending)) logger::write("title_label: Co-op drawn into the %s title atlas", pending.language.c_str());
    else logger::write("title_label: the title atlas could not be locked, the row keeps its label");
    pending.texture->Release();
}

}  // namespace title_label
