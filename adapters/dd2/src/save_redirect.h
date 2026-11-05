#pragma once
// A co-op guest's saves go to <game>\coop\session (DD2 design launcher.md 6): the native SaveService's mount path is set
// there before the title loads anything (set_SaveDirectoryPath is a retail stub; the directory follows the mount), so the
// guest's game reads the host's world the launcher put in coop\session\win64_save and never writes the real save.
#include <cstdint>

namespace save_redirect {

// Turns the redirect on (a co-op guest); tried at once and then from the tick until the SaveService exists.
void enable();

// Game thread: sets the mount once; logs `save_paths: dir=.. mount=..` before and after, and REFUSED when the paths
// read back are not the session folder.
void tick();

// The redirect was set and verified.
bool verified();

}  // namespace save_redirect
