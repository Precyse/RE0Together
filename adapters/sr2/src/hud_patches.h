#pragma once

// The exe edits of milestone 16 wave 2 (docs/SR2_NOTES.md, "HUD and waypoints (milestone 16 wave 2)"): the minimap and pause map
// arrows, the back prompt, the map distance and the waypoint senders for 3 or more players. The tables are in hud_sites.h; each is
// one set, byte-checked and written all or nothing, and only with the milestone 4 partner logic.
namespace hud_patches {

// Writes the sets (the client name trace only with `trace`); a set whose bytes differ is left out. Each result is logged.
void install(bool trace);

}  // namespace hud_patches
