#pragma once

// Party commands, read from the same input the game reads: the keyboard as its own DirectInput read reports it
// (virtual_keys::realKeyDown; foreground-only, so keys typed into another window never count) and the controller
// (pad_commands: Y and LT; the game reads XInput with or without the focus, and so do the commands). The game reads
// them through the focused character's think, whose input on a non-owner's machine is the remote pad, so under
// co-op the adapter handles them: the partner key toggles party_mode, and the switch key is only hidden (every player
// keeps their own camera, camera_parity). Both
// keys are hidden from the game's DirectInput keyboard while a peer is connected, so neither this machine's think nor
// the peer's replay of this pad starts the game's own switch (a swap in the room, the Change phase apart) or orders
// the partner, which is the other player's character. The controller's buttons are hidden the same way. All of this
// only in gameplay (Main phase, no menu): in menus and other screens the keys keep the game's own meaning.
// F9 starts a resync (resync.h) under the same conditions; the game does not use it, so it is not hidden.
// The states are polled from the net thread (a ~5 ms tick, independent of the game's move() ticking); the requests are
// queued for the game thread.
namespace command_input {

// Loads the key bindings.
void enable();

// Net thread, every tick: publishes the focused character and turns key presses into requests.
void onNetTick();

}  // namespace command_input
