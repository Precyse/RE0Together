#pragma once
// The co-op profile (DD2 design progression.md 2-3): the guest's own Arisen and main pawn (their contexts, items, money
// and equipment set) carried into the host's world.
//
// Capture (every launch but a co-op guest's): each time the game saves, the contexts and item records it wrote for the
// Arisen and the main pawn are read from the save sections, as the engine's JSON, into <game>\coop\profile\profile.dd2p.
// Splice (a co-op guest): every load of the host's world runs the two loaders with the profile's pieces in place of the
// host's characters (who the record is and where it stands stay the world's), then the Arisen's level is checked.
#include <cstdint>

namespace profile {

enum class Mode : uint8_t { Capture, Splice };

// False (logged) when the hooks or rows are missing or, to splice, the profile is missing or from another build.
bool install(Mode mode);

// Game thread, from the tick: writes a capture once both sections arrived; checks a splice once in gameplay.
void tick(uint64_t nowMs);

}  // namespace profile
