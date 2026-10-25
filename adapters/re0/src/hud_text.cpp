#include "hud_text.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

#include "imgui_internal.h"
#include "log.h"
#include "paths.h"
#include "ui_art_files.h"

namespace {

using hud_text::Face;

constexpr float kLoadedSize = 16.0f;  // the size the atlas is asked for; every draw names its own size
constexpr size_t kFaceCount = 3;
constexpr uint32_t kByteMax = 255;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;

struct FaceFile {
    const wchar_t* name;
    ImFont* font;   // null: the default font
    bool reported;  // the missing file was logged
};

std::array<FaceFile, kFaceCount> g_faces{{{ui_art_files::kCinzelMedium, nullptr, false},
                                          {ui_art_files::kCinzelBold, nullptr, false},
                                          {ui_art_files::kCormorantItalic, nullptr, false}}};

ImFont* fontOf(Face face) {
    ImFont* font = face == Face::Plain ? nullptr : g_faces[static_cast<size_t>(face)].font;
    return font ? font : ImGui::GetFont();
}

// One copy of the text; with spacing each character is placed on its own.
void drawPass(ImDrawList* list, ImFont* font, float size, ImVec2 pos, std::string_view text, float spacing, ImU32 colour) {
    if (spacing == 0.0f) {
        list->AddText(font, size, pos, colour, text.data(), text.data() + text.size());
        return;
    }
    ImFontBaked* baked = font->GetFontBaked(size);
    const char* at = text.data();
    const char* const end = at + text.size();
    while (at < end) {
        unsigned int code = 0;
        const int bytes = ImTextCharFromUtf8(&code, at, end);
        list->AddText(font, size, pos, colour, at, at + (bytes > 0 ? bytes : 1));
        pos.x += baked->GetCharAdvance(static_cast<ImWchar>(code)) + spacing;
        at += bytes > 0 ? bytes : 1;
    }
}

}  // namespace

namespace hud_text {

float pixel(float scale) { return std::max(1.0f, std::round(scale)); }

ImU32 colour(uint32_t rgb, float alpha) {
    const uint32_t a = static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * kByteMax + 0.5f);
    return IM_COL32((rgb >> kRedShift) & kByteMax, (rgb >> kGreenShift) & kByteMax, rgb & kByteMax, a);
}

void load() {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->AddFontDefault();  // stays the first font: the panel and every fallback use it
    const std::wstring folder = gameDirectory() + L"\\" + ui_art_files::kFontFolder + L"\\";
    for (FaceFile& face : g_faces) {
        const std::wstring path = folder + face.name;
        face.font = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? nullptr : atlas->AddFontFromFileTTF(utf8(path).c_str(), kLoadedSize);
        if (!face.font && !face.reported) logger::write("hud_text: %ls is missing or unreadable, its text uses the default font", path.c_str());
        face.reported = face.reported || !face.font;
    }
}

ImVec2 measure(Face face, float size, std::string_view text, float spacing) {
    ImFont* font = fontOf(face);
    ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
    if (spacing != 0.0f && !text.empty()) extent.x += spacing * static_cast<float>(ImTextCountCharsFromUtf8(text.data(), text.data() + text.size()));
    return extent;
}

float ascent(Face face, float size) { return fontOf(face)->GetFontBaked(size)->Ascent; }

void draw(ImDrawList* list, Face face, float size, ImVec2 pos, std::string_view text, float spacing, ImU32 colour, float shadow) {
    ImFont* font = fontOf(face);
    if (shadow > 0.0f) {
        drawPass(list, font, size, ImVec2(pos.x + shadow, pos.y + shadow), text, spacing, IM_COL32(0, 0, 0, colour >> IM_COL32_A_SHIFT));
    }
    drawPass(list, font, size, pos, text, spacing, colour);
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

std::string utf8(const std::wstring& text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

}  // namespace hud_text
