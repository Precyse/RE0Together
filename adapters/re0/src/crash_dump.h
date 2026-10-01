#pragma once

// When the game crashes, writes a minidump to <game dir>\coop\crash-<date>-<time>.dmp and a line to adapter.log,
// then hands the crash on to whatever filter was installed before (the game's or Steam's reporter).
namespace crash_dump {

// Safe to call again: the game may install its own filter after us, and a second call puts ours back in front.
void install();

}  // namespace crash_dump
