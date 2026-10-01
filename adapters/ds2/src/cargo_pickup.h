#pragma once
// Pickups of the world's loose cargo, kept the same in both worlds; the host decides. When the guest picks up a piece
// that was lying in the world, it asks the host, which deletes the same piece (same kind, same place) from its own
// world and accepts, or refuses when it has no such piece (someone has it already); a refused piece is taken off the
// guest's backpack again. When the host picks one up, the guests delete their copy. Pieces a player put down itself
// come back without asking (drops are not mirrored yet).
//
// The pickup itself runs at once (the game's own action) and is undone on refusal: the adapter watches the backpack
// and the loose pieces around the player rather than holding the game's pickup action.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace cargo_pickup {

constexpr uint16_t kMsgPickup = proto::kFirstGameType + 4;        // 0x0104, guest to host, reliable: Pickup
constexpr uint16_t kMsgPickupResult = proto::kFirstGameType + 5;  // 0x0105, host to guest, reliable: PickupResult
constexpr uint16_t kMsgHostPickup = proto::kFirstGameType + 6;    // 0x0106, host to all, reliable: Pickup (request 0)

struct Pickup {
    uint32_t request;   // the guest's number for this pickup, echoed in the result
    uint32_t type;      // the kind of cargo
    float position[3];  // where the piece lay (world metres)
};
static_assert(sizeof(Pickup) == 20);

struct PickupResult {
    uint32_t request;
    uint32_t accepted;  // 1 = the host took the piece out of its world, 0 = refused
};
static_assert(sizeof(PickupResult) == 8);

// Net thread.
void onFrame(NetClient& net, const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace cargo_pickup
