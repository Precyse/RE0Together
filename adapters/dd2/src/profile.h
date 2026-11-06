#pragma once
// The co-op profile (DD2 design progression.md 2-3): the guest's own Arisen and main pawn (their contexts, items, money
// and equipment set) carried into the host's world.
//
// Capture (every launch but a co-op guest's): each time the game loads or saves, the contexts and item records of the
// Arisen and the main pawn are read from the save sections, as the engine's JSON, into the launch's character file
// (src/save_folders.h: coop\profile\profile.dd2p for the game's own save, the campaign's for a campaign).
// Splice (a co-op guest): every load of the host's world runs the two loaders with the profile's pieces in place of the
// host's characters (who the record is and where it stands stay the world's), then the Arisen's level is checked.
#include <cstdint>
#include <string>

namespace profile {

enum class Mode : uint8_t { Capture, Splice };

// `path` is the character file written (capture) or read (splice). False (logged) when the hooks or rows are missing or, to
// splice, the profile is missing or from another build.
bool install(Mode mode, const std::wstring& path);

// Game thread, from the tick: writes a capture once both sections arrived; checks a splice once in gameplay.
void tick(uint64_t nowMs);

}  // namespace profile
