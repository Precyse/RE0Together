#pragma once

// Party command keys (the game's switch and partner-stay keys) read straight from the keyboard. The game reads
// them through the focused character's think, whose input on a non-owner's machine is the remote pad, so under
// co-op the adapter handles them: switch becomes camera_parity's request, the partner key toggles party_mode. The
// switch key is hidden from the game's keyboard while a peer is connected, so neither this machine's think nor the
// peer's replay of this pad can start the game's own switch.
// The keys are polled from the net thread (a ~5 ms tick, independent of the game's move() ticking, so menus,
// doors and freezes cannot leave the edge detector stale); the requests are queued for the game thread.
namespace command_input {

// Loads the key bindings.
void enable();

// Net thread, every tick: publishes the focused character and turns key presses into requests.
void onNetTick();

}  // namespace command_input
