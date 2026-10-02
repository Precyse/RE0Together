#pragma once
// DS2-internal: what the engine's player code needs so a second player entity does not corrupt memory or act on Sam:
// the player id table sized for more than one player, the art part toggle bound-checked for the remote's body, and the
// components that act on shared state silenced for the remote.
namespace remote_guards {

// Start-up, before the engine builds the player id table.
void installEarly();

// Unsubscribes the remote's components that write the engine's shared HUD block, Sam's inventory or Sam's use
// prompts from every entity message. Call once the remote entity exists.
void silenceRemote();

}  // namespace remote_guards
