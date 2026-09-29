#pragma once
#include <cstdint>

#include "control_rule.h"
#include "net_client.h"
#include "protocol.h"

namespace character_owner {

constexpr uint16_t kMsgOwnership = proto::kFirstGameType + 2;

enum class Character : uint8_t { Billy = 0, Rebecca = 1, Unknown = 0xFF };

// Wire payload of OWNERSHIP (0x0102), host to all: the slot that drives each character. Fixed: the host owns
// Rebecca and the first peer owns Billy, whoever is focused.
struct Ownership {
    uint8_t billyOwnerSlot;
    uint8_t rebeccaOwnerSlot;
};
static_assert(sizeof(Ownership) == 2);

// Class of a uPlayerBase object, or Unknown.
Character identify(uintptr_t player);

// The controlled or partner object of the given class, or 0.
uintptr_t find(Character character);

// The character this machine's player owns (Unknown until ownership is known or without a peer).
Character localCharacter();

// True on the linked session's host.
bool isHost();

// True when `slot` is the linked session's host.
bool isHostSlot(uint8_t slot);

using control_rule::Control;

// Display name of a character.
const char* name(Character character);

// Who drives a character on this machine right now: its owner, except that in LEAVE_BEHIND the unfocused character
// is Locked (see control_rule.h). The one place that rule is applied.
Control controlOf(Character character);

// Makes `character` the focused character on this machine when it is currently the partner.
void focus(Character character);

// Ownership only, ignoring the party mode: damage, inventory and state sync follow the owner even while the
// character waits.
bool isRemoteOwned(Character character);
bool isLocalOwned(Character character);

// Net thread: tracks slots and epoch; a new peer or epoch makes the host recompute ownership.
void onSession(const SessionSnapshot& session);

// Net thread: applies OWNERSHIP from the host.
void onFrame(const GameFrame& frame);

// Registers the per-frame host decision. Must be registered before the callbacks that read ownership.
void enable(NetClient& net);

}  // namespace character_owner
