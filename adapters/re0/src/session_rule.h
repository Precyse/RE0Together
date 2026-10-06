#pragma once
#include <cstdint>

#include "room_phase.h"

// Pure session-flow rules (no game, unit tested): where a save request goes, and what the guest's automatic join does.
namespace session_rule {

enum class SaveRoute {
    Pass,      // not a player's save: the game's own request
    CoopSlot,  // the host saves into the co-op slot
    Refuse,    // a guest's save would land in the session copy, which is deleted when the launcher exits
};

// `guestSession`: this game plays the host's session copy (a guest, also after the host left).
constexpr SaveRoute saveRoute(bool playerSlot, int32_t phase, bool guestSession, bool peerPresent) {
    if (!playerSlot || phase != room_phase::Save) return SaveRoute::Pass;
    if (guestSession) return SaveRoute::Refuse;
    return peerPresent ? SaveRoute::CoopSlot : SaveRoute::Pass;
}

enum class JoinMode {
    Idle,        // nothing to do: in game, or not a guest
    Waiting,     // outside the game while the host is not in game: keyboard muted
    Confirming,  // outside the game while the host plays: virtual Enter
};

// A guest at game over follows the host's Continue always; at boot, title and load list only with the full
// automatic join (a character is controlled = in game).
constexpr JoinMode joinMode(bool guestSession, bool fullJoin, int32_t phase, bool characterControlled, bool hostInGame) {
    if (!guestSession) return JoinMode::Idle;
    const bool outside = phase == room_phase::Dead || (fullJoin && !characterControlled);
    if (!outside) return JoinMode::Idle;
    return hostInGame ? JoinMode::Confirming : JoinMode::Waiting;
}

}  // namespace session_rule
