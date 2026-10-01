#pragma once

// Party commands (the game's switch and Solo/Team) read straight from the keyboard and from the controller
// (pad_commands: Y and LT). The game reads
// them through the focused character's think, whose input on a non-owner's machine is the remote pad, so under
// co-op the adapter handles them: switch becomes camera_parity's request, the partner key toggles party_mode. Both
// keys are hidden from the game's DirectInput keyboard while a peer is connected, so neither this machine's think nor
// the peer's replay of this pad starts the game's own switch (a swap in the room, the Change phase apart) or orders
// the partner, which is the other player's character. The controller's buttons are hidden the same way.
// The keys are polled from the net thread (a ~5 ms tick, independent of the game's move() ticking, so menus,
// doors and freezes cannot leave the edge detector stale); the requests are queued for the game thread.
namespace command_input {

// Loads the key bindings.
void enable();

// Net thread, every tick: publishes the focused character and turns key presses into requests.
void onNetTick();

}  // namespace command_input
