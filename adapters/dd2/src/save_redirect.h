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

// The redirect was set and verified.
bool verified();

}  // namespace save_redirect
