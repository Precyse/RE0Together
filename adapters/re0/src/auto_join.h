#pragma once

// A guest joining a session goes straight into the host's game: while the guest's game is outside gameplay (logos,
// title, load list, continue prompt) the adapter confirms each screen with a virtual Enter and mutes the real
// keyboard, and session_slot turns the load into the host's slot. It stops as soon as a character is controlled.
namespace auto_join {

// Net thread, every tick (the game tick does not run outside gameplay).
void onNetTick();

}  // namespace auto_join
