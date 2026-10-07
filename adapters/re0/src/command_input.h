#pragma once

// Party commands read straight from the keyboard and from the controller
// (pad_commands: Y and LT). The game reads
// them through the focused character's think, whose input on a non-owner's machine is the remote pad, so under
// co-op the adapter handles them: the partner key toggles party_mode, and the switch key is only hidden (every player
// keeps their own camera, camera_parity). Both
// keys are hidden from the game's DirectInput keyboard while a peer is connected, so neither this machine's think nor
// the peer's replay of this pad starts the game's own switch (a swap in the room, the Change phase apart) or orders
// the partner, which is the other player's character. The controller's buttons are hidden the same way. All of this
// only in gameplay (Main phase, no menu): in menus and other screens the keys keep the game's own meaning.
// F9 starts a resync (resync.h) under the same conditions; the game does not use it, so it is not hidden.
// The keys are polled from the net thread (a ~5 ms tick, independent of the game's move() ticking, so menus,
// doors and freezes cannot leave the edge detector stale); the requests are queued for the game thread.
namespace command_input {

// Loads the key bindings.
void enable();

// Net thread, every tick: publishes the focused character and turns key presses into requests.
void onNetTick();

}  // namespace command_input
