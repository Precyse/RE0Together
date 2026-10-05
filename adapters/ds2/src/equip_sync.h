#pragma once
// The gear each player wears and carries on the body. It is baggage pieces in the visible slots of the player's
// baggage owner (EDSBaggageSlotType: RightArm 4, LeftArm 5, RightWaist 6, LeftWaist 7, MainWeapon 10, Handgun 11,
// SubWeaponPouch 12, Tool 13, Shoes 14, Skeleton 17, Glove 18, Mask 19). Each machine reports its local player's pieces
// in those slots (EQUIP_STATE, reliable, on change); the partner's body gets the same kinds created in the same slots
// of its own owner, and a piece that is gone is deleted from there, so the body wears and carries what its partner does. The pieces belong only to
// the body's owner, never to the local inventory. A piece created in a hand slot (8, 9) is ejected by the game within
// a second or two, so a weapon drawn in the hand is not mirrored yet.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace equip_sync {

constexpr uint16_t kMsgEquipState = proto::kFirstGameType + 12;  // 0x010C, to all, reliable: EquipHeader, then entries
constexpr uint8_t kMirroredSlots[] = {4, 5, 6, 7, 10, 11, 12, 13, 14, 17, 18, 19};
constexpr uint32_t kMaxHeld = 24;

struct EquipHeader {
    uint32_t count;
    uint32_t reserved;
};
static_assert(sizeof(EquipHeader) == 8);

// One worn or carried piece: the slot kind it is in and its cargo kind (the same on every machine).
struct Held {
    uint8_t slot;
    uint8_t reserved[3];
    uint32_t type;

    bool operator==(const Held&) const = default;
};
static_assert(sizeof(Held) == 8);

// Start-up: registers the simulation-thread callback that reads the pieces and builds the body's.
void installEarly();

// Net thread: the partner's report in, ours out.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace equip_sync
