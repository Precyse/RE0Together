#pragma once

// Session saves: when <game dir>\coop\session\Documents exists, the game's "Documents" folder (where DEATH STRANDING
// 2 keeps its saves) is that folder instead, so a co-op session plays and autosaves a session copy and never writes
// the player's own saves. Both shell APIs the game imports are covered (SHGetKnownFolderPath for FOLDERID_Documents,
// SHGetFolderPathW for CSIDL_PERSONAL). A MinHook installed from the init thread was too late: the game had already
// cached the real folder.
namespace documents_redirect {

// From DllMain, before any game code runs (the game resolves its save folder at start-up): when the session folder
// exists, points the game's own import slots for both functions at the redirect. No library is loaded, so it is
// safe under the loader lock.
void install();

// Whether saves go to the session folder in this run (for the log).
bool active();

}  // namespace documents_redirect
