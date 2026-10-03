#pragma once
// Loose cargo on the ground, kept the same in both worlds; the host decides. A piece a player puts down appears at the
// same spot in the other world (CARGO_DROP), once that world's player is near enough for the ground there to be
// loaded (an order piece is matched by its order id, any other by kind and place). When the guest picks up a piece, it asks the host, which deletes the same piece (same kind, same place)
// from its own world and accepts, or refuses when it has no such piece (someone has it already); a refused piece is
// taken off the guest's backpack again. When the host picks one up, the guests delete their copy.
//
// Pickups and drops run at once (the game's own actions) and pickups are undone on refusal: the adapter watches the
// backpack and the loose pieces around the player rather than holding the game's actions.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace cargo_ground {

constexpr uint16_t kMsgPickup = proto::kFirstGameType + 4;        // 0x0104, guest to host, reliable: Spot
constexpr uint16_t kMsgPickupResult = proto::kFirstGameType + 5;  // 0x0105, host to guest, reliable: PickupResult
constexpr uint16_t kMsgHostPickup = proto::kFirstGameType + 6;    // 0x0106, host to all, reliable: Spot (request 0)
constexpr uint16_t kMsgDrop = proto::kFirstGameType + 7;          // 0x0107, to all, reliable: Spot (request 0)

// A piece of cargo at a place in the world.
struct Spot {
    uint32_t request;   // the guest's number for a pickup, echoed in the result; 0 otherwise
    uint32_t type;      // the kind of cargo
    float position[3];  // world metres
    uint32_t reserved;
    uint64_t orderId;   // the order the piece belongs to, 0 for plain cargo: such a piece is matched by this, not by place
};
static_assert(sizeof(Spot) == 32);

struct PickupResult {
    uint32_t request;
    uint32_t accepted;  // 1 = the host took the piece out of its world, 0 = refused
};
static_assert(sizeof(PickupResult) == 8);

// Net thread.
void onFrame(NetClient& net, const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace cargo_ground
