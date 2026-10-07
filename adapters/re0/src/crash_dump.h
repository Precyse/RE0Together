#pragma once
#include <windows.h>

// When the game crashes, writes a minidump to <game dir>\coop\crash-<date>-<time>.dmp and a line to adapter.log,
// then hands the crash on to whatever filter was installed before (the game's or Steam's reporter).
namespace crash_dump {

// Safe to call again: the game may install its own filter after us, and a second call puts ours back in front.
void install();

// For an __except filter that catches the exception: logs `what` with the exception code, the faulting address (raw,
// which is the image VA in the non-ASLR game, and module+offset) and for an access violation the address touched.
// The first caught exception of the session is also written to coop\caught-<date>-<time>.dmp.
// Returns EXCEPTION_EXECUTE_HANDLER.
LONG reportCaught(EXCEPTION_POINTERS* info, const char* what);

}  // namespace crash_dump
