#pragma once
// The title menu's rules (pure, unit tested): when the plugin presses Continue for a guest, which row that is, and which row a guest
// may not choose. The menu is the game's GUI app.ui030201; a row is an app.ui030201.ListInfo whose ItemType is one of
// app.ui030201.ListItemType (enum literals read at run time, kUnknownLiteral when one is missing).
#include <cstdint>
#include <span>

namespace title_menu_rule {

constexpr int32_t kUnknownLiteral = INT32_MIN;

// The menu has been idle (waiting for a choice, nothing loading or saving) this long before Continue is pressed: the boot screens
// and the game's own first loads are done.
constexpr uint64_t kSettleMs = 3000;

// What one frame of the menu shows.
struct Menu {
    bool selectMenu;  // the GUI's flow is SelectMenu: the rows are shown and wait for a choice
    bool saveIdle;    // the SaveService has nothing running
    bool safeToLoad;  // nothing is loaded from the real save folder (save_redirect::safeToLoad)
};

inline bool waiting(const Menu& menu) { return menu.selectMenu && menu.saveIdle && menu.safeToLoad; }

// Continue is pressed once the menu has been waiting for kSettleMs; `waitingSinceMs` is 0 while it is not waiting.
inline bool settled(uint64_t waitingSinceMs, uint64_t nowMs) { return waitingSinceMs != 0 && nowMs - waitingSinceMs >= kSettleMs; }

// The index of the first row of `itemTypes` that is `wanted`, -1 when there is none (or the literal is unknown).
inline int rowOf(std::span<const int32_t> itemTypes, int32_t wanted) {
    if (wanted == kUnknownLiteral) return -1;
    for (size_t i = 0; i < itemTypes.size(); ++i) {
        if (itemTypes[i] == wanted) return static_cast<int>(i);
    }
    return -1;
}

// A guest never starts a world of its own: the New Game rows are refused.
inline bool blocksNewGame(bool guest, int32_t itemType, int32_t newGame, int32_t initGame) {
    return guest && itemType != kUnknownLiteral && (itemType == newGame || itemType == initGame);
}

}  // namespace title_menu_rule
