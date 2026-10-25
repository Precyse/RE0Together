#pragma once

// Shot probe of partner_mode=state (docs/RE0_NOTES.md "Shot probe"): around the shotgun's wide hit scan, a scan that hit
// no enemy logs where the nearest living enemy's hit shapes were against the shot's ray and against the enemy's own
// position and matrix. Budgeted like enemy_drift; nothing is logged for a scan that hit.
namespace shot_probe {

// Hooks sWeapon::hitscanWide, the ray it builds and the shape ray test. False when a hook could not be installed.
bool enable();

}  // namespace shot_probe
