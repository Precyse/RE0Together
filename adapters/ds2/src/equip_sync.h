#pragma once
// The weapons each player carries on the body. They are baggage pieces in the holster slots of the player's baggage
// owner (RightArm 4, LeftArm 5, RightWaist 6, LeftWaist 7). Each machine reports its local player's holster pieces
// (EQUIP_STATE, reliable, on change); the partner's body gets the same kinds created in the holster slots of its own
// owner, and a piece that is gone is deleted from there, so the body shows the same weapons. The pieces belong only to
// the body's owner, never to the local inventory. A piece created in a hand slot (8, 9) is ejected by the game within
// a second or two, so a weapon drawn in the hand is not mirrored yet.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace equip_sync {

constexpr uint16_t kMsgEquipState = proto::kFirstGameType + 12;  // 0x010C, to all, reliable: EquipHeader, then entries
constexpr uint8_t kMirroredSlots[] = {4, 5, 6, 7};
constexpr uint32_t kMaxHeld = 8;

struct EquipHeader {
    uint32_t count;
    uint32_t reserved;
};
static_assert(sizeof(EquipHeader) == 8);

// One carried piece: the holster slot kind it is in and its cargo kind (the same on every machine).
struct Held {
    uint8_t slot;
    uint8_t reserved[3];
    uint32_t type;

    bool operator==(const Held&) const = default;
};
static_assert(sizeof(Held) == 8);

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace equip_sync
