#pragma once
#include "live_menu_rule.h"

// Live menus (adapter.ini live_menus, state mode only; docs/RE0_NOTES.md "Live menus"): with a peer connected the
// inventory, files, item pickup prompt and map no longer stop the world. The game stops it there with sUnit stop
// reasons 7 and 8; this module clears them every frame while such a screen is open, holds the local character's
// input meanwhile (it stands still, enemies can still reach it) and gives the stop back once it is hurt.
namespace live_menus {

// Records the switch (configured and state mode) and checks the stop reason setter's bytes. Call once at start.
void configure(bool on);

// What the local screen does to the world now (live_menu_rule::Screen). Any thread.
live_menu_rule::Screen localScreen();

// Game thread, once per frame from the sUnit::updateAll detour (menu_mirror), before the units update.
void onUnitUpdate();

// Game thread: true while a live menu keeps the world running, so the local character's move reads the neutral pad.
bool inputHeld();

}  // namespace live_menus
