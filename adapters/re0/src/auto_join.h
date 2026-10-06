#pragma once

// The game over screen: the guest waits there (muted) until the host is playing again, then a virtual Enter takes
// the Continue cursor into the host's save (session_slot turns the load into the host's slot), so both machines take
// the host's choice. With the full automatic join (adapter.ini auto_join=1) a guest joining a session goes straight
// into the host's game the same way: while its game is outside gameplay (logos, title, load list, continue prompt) the
// adapter confirms each screen and mutes the real keyboard until a character is controlled.
namespace auto_join {

// Turns the full automatic join on (adapter.ini auto_join=1); without it only the game over screen is handled.
void enable();

// Net thread, every tick (the game tick does not run outside gameplay).
void onNetTick();

}  // namespace auto_join
