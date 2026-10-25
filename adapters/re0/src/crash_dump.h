#pragma once
#include <windows.h>

// When the game crashes, writes a minidump to <game dir>\coop\crash-<date>-<time>.dmp and a line to adapter.log,
// then hands the crash on to whatever filter was installed before (the game's or Steam's reporter). A game that stops
// drawing frames for hang_rule::kHangMs while its window is foreground gets coop\hang-<date>-<time>.dmp
// (docs/RE0_NOTES.md "Hang watchdog").
namespace crash_dump {

// Safe to call again: the game may install its own filter after us, and a second call puts ours back in front.
void install();

// For an __except filter that catches the exception: logs `what` with the exception code, the faulting address (raw,
// which is the image VA in the non-ASLR game, and module+offset) and for an access violation the address touched.
// The first caught exception of the session is also written to coop\caught-<date>-<time>.dmp.
// Returns EXCEPTION_EXECUTE_HANDLER.
LONG reportCaught(EXCEPTION_POINTERS* info, const char* what);

// Render thread, once per drawn frame: the hang watchdog's beat. Logs once, at the first beat with a known `gameThread`
// (the game tick's thread, 0 until it has run), both threads, so a log shows whether the frame is drawn on the game thread.
void heartbeat(DWORD gameThread);

// True while the game window is in front: a stall only counts then.
using Foreground = bool (*)();

// Starts the watchdog thread (polls once a second). Call once; a hang is dumped in every partner mode.
void startHangWatch(Foreground foreground);

}  // namespace crash_dump
