#pragma once
// The adapter's game-thread tick: a hook on ClientNet::ProcessRecvQueue, which the main thread runs once per frame
// (the chain WinApp vf9 -> ... -> ProcessRecvQueue was traced on the 0x6AB28F00 build; once per frame is checked by
// the thread line it logs).
// Each call runs every module's tick in a fixed order after the game's own queue work.
namespace main_tick {

// Installs the hook (inside the caller's hooks batch); false when the site bytes differ. `devCommands` arms dev_cmd.
bool install(bool devCommands);

}  // namespace main_tick
