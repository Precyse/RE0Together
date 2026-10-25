#pragma once

// A guest whose host left the session, or whose host's game left play (its title, closed or crashed), goes back to the
// main menu (docs/RE0_NOTES.md "Host left"): the way the game's
// own game over leaves its world, the title flag 0xdcc131 then room phase Exit, which opens the title at its main menu.
// Nothing played on without the host would reach the host's save.
namespace host_exit {

// Registers the game tick step that leaves the world once plain gameplay allows it.
void enable();

// Any thread: the host left; the toast says so and the guest leaves at the next safe moment.
void request();

}  // namespace host_exit
