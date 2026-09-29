#pragma once

namespace save_redirect {

// Called after the game wrote a file to the real Steam cloud (not a session file), with the file name.
using WriteListener = void (*)(const char* name);

// Points the game's SteamRemoteStorage import at a proxy. Files present in <game dir>\coop\session are served from
// that folder instead of the Steam cloud (the guest plays the host's save); every other call goes to Steam, and a
// successful cloud write is reported to `onCloudWrite` (the host shares its new save). False when the import
// cannot be patched.
bool install(WriteListener onCloudWrite);

// Restores the original import.
void uninstall();

}  // namespace save_redirect
