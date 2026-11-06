#pragma once
// A launch that plays a world other than its own save (a co-op guest: coop\session; a host's campaign: coop\campaigns\<id>;
// src/save_folders.h) has the native SaveService's mount path set there before the title loads anything
// (set_SaveDirectoryPath is a retail stub; the directory follows the mount), so the game reads and writes <mount>\win64_save
// and never the real save.
#include <cstdint>
#include <string>

namespace save_redirect {

// Turns the redirect to `mount` on; tried at once and then from the tick until the SaveService exists.
void enable(const std::wstring& mount);

// Game thread: sets the mount once; logs `save_paths: dir=.. mount=..` before and after, and REFUSED when the paths
// read back are not the mount's.
void tick();

// The redirect was set and verified.
bool verified();

}  // namespace save_redirect
