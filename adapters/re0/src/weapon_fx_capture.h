#pragma once

// Owner side of the weapon effects of partner_mode=state: the effect primitives the owned character's fire, reload and
// dry-fire handlers call (sound, muzzle flash, casing / magazine / projectile, effects, molotov stow and wick, water
// ripple) are recorded at the allow-listed call sites (weapon_fx_sites.h) and sent as one WEAPON_FX per frame from the
// character's post-move. The original always runs first; the shot, ammo, shot counter and rumble are never touched.
namespace weapon_fx_capture {

// Hooks the eight primitives and registers the post-move flush. Call before game_tick::install, after MinHook is ready.
bool enable();

void uninstall();

}  // namespace weapon_fx_capture
