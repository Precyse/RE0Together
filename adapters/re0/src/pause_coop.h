#pragma once

// The pause menu's Co-op row (docs/RE0_NOTES.md "Pause menu: the Co-op row"): a copy of the Quit Game row between Back
// and Quit Game, and the pause's input rewritten for three rows. Co-op closes the pause like Back, then opens the
// in-game Options screen on the native Co-op page (coop_menu_rule::kInGameMenuId) with the world held the way the
// status screen holds it, until the page closes.
namespace pause_coop {

// Patches uGUIPause's vtable slots 8 and 55 (each checked to hold the expected function first). False when the vtable
// is not the expected one (the vanilla pause stays).
bool enable();

// Game thread, once per rendered frame (a debug_overlay frame step, registered by enable): opens the page once the pause has closed and lets the
// world go once the Options screen closed. Not the game tick: that runs from the player's move, which the hold stops.
void onFrame();

}  // namespace pause_coop
