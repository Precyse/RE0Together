#pragma once

// The camera. With a peer, each machine always keeps its own character focused (the camera follows it), whatever the
// party mode: Team and Split up only decide how doors are travelled. While a peer is connected the game never sees
// the keyboard switch key (command_input), so only this module switches the focus.
namespace camera_parity {

// Registers the per-frame focus keeping.
void enable();

}  // namespace camera_parity
