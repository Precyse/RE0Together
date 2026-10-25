#pragma once

// Where the overlay's art and fonts are in the game folder. The launcher writes coop\ui from the player's own install at
// launch (launcher/UiArt.cs, the same names) and installs coop\fonts with the mod (launcher/games/re0.json adapterFiles).
// Paths are relative to the game folder.
namespace ui_art_files {

constexpr wchar_t kFontFolder[] = L"coop\\fonts";
constexpr wchar_t kCinzelMedium[] = L"Cinzel-Medium.ttf";
constexpr wchar_t kCinzelBold[] = L"Cinzel-Bold.ttf";
constexpr wchar_t kCormorantItalic[] = L"CormorantGaramond-MediumItalic.ttf";

constexpr wchar_t kArtFolder[] = L"coop\\ui";
constexpr wchar_t kGameFontLayout[] = L"font_eng.gfd";
constexpr wchar_t kGameFontImage[] = L"font_eng.png";
constexpr wchar_t kLeather[] = L"dialog_leather.png";
constexpr wchar_t kRodTop[] = L"dialog_rod_top.png";
constexpr wchar_t kRodBottom[] = L"dialog_rod_bottom.png";
constexpr wchar_t kOutfitFolder[] = L"outfits";
constexpr wchar_t kOutfitExtension[] = L".png";

}  // namespace ui_art_files
