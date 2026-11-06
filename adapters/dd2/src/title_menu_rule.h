#pragma once
// The title menu's guard (pure, unit tested): which row a guest may not choose. The menu is the game's GUI app.ui030201; a row is
// an app.ui030201.ListInfo whose ItemType is one of app.ui030201.ListItemType (enum literals read at run time, kUnknownLiteral
// when one is missing).
#include <cstdint>

namespace title_menu_rule {

constexpr int32_t kUnknownLiteral = INT32_MIN;

// A guest never starts a world of its own: the New Game rows are refused.
inline bool blocksNewGame(bool guest, int32_t itemType, int32_t newGame, int32_t initGame) {
    return guest && itemType != kUnknownLiteral && (itemType == newGame || itemType == initGame);
}

}  // namespace title_menu_rule
