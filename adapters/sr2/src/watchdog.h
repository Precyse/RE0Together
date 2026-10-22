#pragma once

// The game thread watchdog (docs/SR2_NOTES.md, "Watchdog"), always on: the game's frame 0x697780 stamps the time; a thread
// that sees no frame for 10 s writes coop\dumps\<stamp>.dmp (MiniDumpWithThreadInfo, the system dbghelp) and <stamp>.txt
// (the reason, the stall time, the game thread's registers and stack top), keeps the newest 3 and logs the stall and its end.
// ExitProcess is hooked to log the exit code, so a silent exit leaves a line and a kill leaves none.
namespace watchdog {

// Checks the frame's bytes and queues its hook and the ExitProcess hook (inside the start-up batch); starts the watch thread.
void install();

}  // namespace watchdog
