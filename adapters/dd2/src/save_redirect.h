#pragma once
// A launch that plays a world other than its own save (a co-op guest: coop\session\win64_save; a host's campaign:
// coop\campaigns\<id>\win64_save; src/save_folders.h) has the native SaveService's mount set to that save folder before the
// title loads anything (the mount is the save folder itself: the game's own reads back "./win64_save"; set_SaveDirectoryPath
// is a retail stub), so the game reads and writes there and never the real save.
#include <cstdint>
#include <string>

namespace save_redirect {

// Turns the redirect to `mount` on; tried at once and then from the tick until the SaveService exists.
void enable(const std::wstring& mount);

// Game thread, every step: sets the mount once the SaveService exists, then every 2 s sets it again if the game reset it
// (logged with a count); logs `save_paths: dir=.. mount=..` before and after, and REFUSED when the paths
// read back is not the folder asked for.
void tick(uint64_t nowMs);

// Off: no other save folder expected. Pending: expected, not set yet (the SaveService does not exist, or the game just
// reset it). Verified: set and read back. Refused: set and not the folder asked for, or the SaveService calls are missing.
enum class State : uint8_t { Off, Pending, Verified, Refused };
State state();

// Any thread: nothing is loaded from or saved to the real save folder (no redirect expected, or it is verified). The
// co-op character is spliced into a load only when this holds.
bool safeToLoad();

}  // namespace save_redirect
