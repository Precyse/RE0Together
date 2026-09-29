#pragma once

namespace save_redirect {

// Points the game's SteamRemoteStorage import at a proxy that serves files present in <game dir>\coop\session
// from that folder instead of the Steam cloud. Returns false, changing nothing, when the folder holds no files.
bool install();

// Restores the original import.
void uninstall();

}  // namespace save_redirect
