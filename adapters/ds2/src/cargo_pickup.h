#pragma once
// Guest pickups confirmed by the host. The world's loose cargo is the host's: when the guest picks up a piece that
// was lying in the world, it asks the host, which deletes the same piece (same kind, same place) from its own world
// and accepts, or refuses when it has no such piece (the host or nobody has it any more); a refused piece is taken
// off the guest's backpack again. Pieces the guest dropped itself come back without asking.
//
// The pickup itself runs at once on the guest (the game's own action) and is undone on refusal: the adapter watches
// the backpack and the loose pieces around the player rather than holding the game's pickup action.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace cargo_pickup {

constexpr uint16_t kMsgPickup = proto::kFirstGameType + 4;        // 0x0104, guest to host, reliable: Pickup
constexpr uint16_t kMsgPickupResult = proto::kFirstGameType + 5;  // 0x0105, host to guest, reliable: PickupResult

struct Pickup {
    uint32_t request;  // the guest's number for this pickup, echoed in the result
    uint32_t type;     // the kind of cargo
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
