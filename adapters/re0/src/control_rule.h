#pragma once
#include <cstdint>

// Pure control rules of the co-op model; no game or network access, so they are unit tested.
namespace control_rule {

// Who drives a character on this machine.
//   Vanilla: no co-op peer, the game behaves normally.
//   Local / Remote: a player drives it; Local reads this machine's pad, Remote replays the owner's input.
//   Locked: nobody drives it (owner unknown on a guest, or left behind), its pad is blocked.
enum class Control : uint8_t { Vanilla, Local, Remote, Locked };

// TEAM: both characters are driven by their owners. LEAVE_BEHIND: the unfocused character waits.
enum class PartyMode : uint8_t { Team, LeaveBehind };

constexpr int kNoOwner = -1;

// Control by ownership only. A host with an undecided owner keeps vanilla control; a guest waits (Locked).
constexpr Control byOwnership(bool peerPresent, bool isHost, int ownerSlot, int localSlot) {
    if (!peerPresent) return Control::Vanilla;
    if (ownerSlot == kNoOwner) return isHost ? Control::Vanilla : Control::Locked;
    return ownerSlot == localSlot ? Control::Local : Control::Remote;
}

// Ownership control adjusted by the party mode: in LEAVE_BEHIND the unfocused driven character is Locked.
constexpr Control byPartyMode(Control owned, PartyMode mode, bool focused) {
    const bool driven = owned == Control::Local || owned == Control::Remote;
    return driven && mode == PartyMode::LeaveBehind && !focused ? Control::Locked : owned;
}

}  // namespace control_rule
