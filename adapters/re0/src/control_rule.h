#pragma once
#include <cstdint>

// Pure control rules of the co-op model; no game or network access, so they are unit tested.
namespace control_rule {

// Who drives a character on this machine.
//   Vanilla: no co-op peer, the game behaves normally.
//   Local / Remote: a player drives it; Local reads this machine's pad, Remote replays the owner's input.
//   Locked: nobody drives it here (owner unknown on a guest, or its owner is in another room), its pad is blocked.
enum class Control : uint8_t { Vanilla, Local, Remote, Locked };

// TEAM: one shared camera and the partner follows through doors. LEAVE_BEHIND: independent play, each machine keeps
// its own player's character in focus and nobody follows (split_rooms.h).
enum class PartyMode : uint8_t { Team, LeaveBehind };

constexpr int kNoOwner = -1;

// Where the other player is, compared with the room loaded here.
enum class PeerPlace : uint8_t { Unknown, Here, Elsewhere };

// Whether this machine runs the enemies of its loaded room: alone in it always; in a shared room the one that was there
// first (its claim), the host when both or neither claim; the host while the peer's room is not known yet. Both
// machines evaluating this with swapped arguments never both answer true (or both false) in a shared room.
constexpr bool runsEnemies(PeerPlace peer, bool isHost, bool ownClaim, bool peerClaim) {
    if (peer == PeerPlace::Elsewhere) return true;
    if (peer == PeerPlace::Unknown) return isHost;
    return ownClaim == peerClaim ? isHost : ownClaim;
}

// Control by ownership only. A host with an undecided owner keeps vanilla control; a guest waits (Locked).
constexpr Control byOwnership(bool peerPresent, bool isHost, int ownerSlot, int localSlot) {
    if (!peerPresent) return Control::Vanilla;
    if (ownerSlot == kNoOwner) return isHost ? Control::Vanilla : Control::Locked;
    return ownerSlot == localSlot ? Control::Local : Control::Remote;
}

// Ownership control adjusted by where the owner is: a remote character whose owner reports another room than the one
// loaded here is Locked, so the owner's input (meant for that room) is not replayed here.
constexpr Control byPresence(Control owned, bool ownerInLoadedRoom) {
    return owned == Control::Remote && !ownerInLoadedRoom ? Control::Locked : owned;
}

}  // namespace control_rule
