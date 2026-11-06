#pragma once
// The co-op profile (DD2 design progression.md 2-3): the guest's own Arisen and main pawn (their contexts, items, money
// and equipment set) carried into the host's world.
//
// Capture (every launch but a co-op guest's): each time the game loads or saves, the contexts and item records of the
// Arisen and the main pawn are read from the save sections, as the engine's JSON, into the launch's character file
// (src/save_folders.h: coop\profile\profile.dd2p for the game's own save, the campaign's for a campaign).
// Splice (a co-op guest): every load of the host's world runs the two loaders with the profile's pieces in place of the
// host's characters (who the record is and where it stands stay the world's), then the Arisen's level is checked. Nothing
// is spliced while the save redirect is not verified (save_redirect::safeToLoad). After a load that was spliced, with no
// piece refused, and checked ok, the guest's own saves go back to the same character file (a temp file moved over it, the
// old one kept as `.bak`), so the next load splices the progress; after any other load they do not.
#include <cstdint>
#include <string>

#include "game_state_rule.h"

namespace profile {

enum class Mode : uint8_t { Capture, Splice };

// `path` is the character file written (capture) or read (splice). False (logged) when the hooks or rows are missing or, to
// splice, the profile is missing or from another build.
bool install(Mode mode, const std::wstring& path);

// The guest's character as GAME_STATE reports it (game_state_rule::Char): off for every launch but a co-op guest's.
game_state_rule::Char spliceState();

// Game thread: captures from the two sections of a save loaded outside the game's own flow (src/offline_capture.h) and
// writes the character file at once; false when a section was not readable or the file was not written.
bool captureFrom(void* contextSection, void* itemSection);

// Game thread, from the tick: checks a splice once in gameplay; writes a capture (or a guest's write-back) once both sections arrived.
void tick(uint64_t nowMs);

}  // namespace profile
