#pragma once

// Two engine hooks that matter for a puppet only (partner_mode=state); for every other unit and think they call straight
// through.
//  - The motion update (vtable slot 31, 0x5286c0) of the puppet inside its own move: after the original, the spine bend
//    by the mirrored manual pitch, which the paused branch of the move skips.
//  - A puppet's think rumble (cPlayerThink slot 15, 0x4fac40): muted. Enemies rumble through the victim's think, which
//    would shake this machine's own pad when the remote character is grabbed.
namespace puppet_hooks {

// Installs both; false (and none installed) when a hook could not be.
bool enable();

void uninstall();

}  // namespace puppet_hooks
