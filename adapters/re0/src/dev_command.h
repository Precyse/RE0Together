#pragma once

// Developer commands for live tests (adapter.ini dev_commands=1, default off): the file coop\cmd.txt, polled about once a
// second and deleted when read, holds one command per line (dev_command_rule.h). All partner modes; no wire.
//   stall <ms>  the next game tick sleeps that long once (1..60000): the hang watchdog's live test
//   lock        sets the trigger and player pause bits in plain gameplay with no script running: the orphan lock the rescue
//               watchdog must release after 4 s
//   rescue      one press of the rescue chord (rescue.h)
namespace dev_command {

// Turns the commands on and registers the game tick step. Call before game_tick::install.
void enable();

// Net thread, every tick: looks for cmd.txt about once a second.
void onNetTick();

}  // namespace dev_command
