#pragma once
// Cargo moved between the two players' racks, decided by the host. Each machine keeps its own Sam and cargo; a
// transfer deletes the piece on the giver's machine and creates the same kind on the receiver's with the game's own
// requests (game::removeCargo / addCargo; an order piece is recreated with its order link). Every player reports what its backpack holds (the host's menu lists the
// guest's, each player draws the partner's on their body); the host gives and takes through cargo_menu, and the guest
// carries out the host's takes.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game.h"
#include "net_client.h"
#include "protocol.h"

namespace cargo_transfer {

constexpr uint16_t kMsgCargoList = proto::kFirstGameType + 1;  // 0x0101, each player to all, reliable: CargoList
constexpr uint16_t kMsgCargoTake = proto::kFirstGameType + 2;  // 0x0102, host to guest, reliable: CargoTake
constexpr uint16_t kMsgCargoAdd = proto::kFirstGameType + 3;   // 0x0103, giver to receiver, reliable: CargoAdd

constexpr size_t kNameBytes = 44;
constexpr uint32_t kMaxListed = 64;

// One carried piece on the wire. A CARGO_LIST payload is a u32 count followed by that many entries.
struct CargoEntry {
    uint64_t handle;
    uint32_t type;
    char name[kNameBytes];  // UTF-8, zero padded, cut at kNameBytes
};
static_assert(sizeof(CargoEntry) == 56);

// The host asks the guest for one of its pieces; the guest deletes it and answers with CARGO_ADD.
struct CargoTake {
    uint64_t handle;
};
static_assert(sizeof(CargoTake) == 8);

// The receiver creates a piece of this kind on its own player; order cargo keeps the order link it had (DSBaggage
// +0x28 / +0x30), so the host's turn-in still counts a piece that went to the partner and came back.
struct CargoAdd {
    uint32_t type;
    uint8_t category;
    uint8_t reserved[3];
    float durability;
    uint32_t reserved2;
    uint64_t orderId;
    uint64_t secondId;
};
static_assert(sizeof(CargoAdd) == 32);

struct Partner {
    uint8_t slot = 0;
    std::vector<game::Cargo> cargo;
};

// Net thread: messages from the partner, and the periodic work (refresh, report, queued transfers).
void onFrame(NetClient& net, const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// Render thread (cargo_menu).
bool isHost();
std::vector<game::Cargo> localCargo();
std::optional<Partner> partner();  // the partner and what it reported carrying (the host's menu needs a host)
void give(const game::Cargo& piece);  // host: its own piece to the guest
void take(const game::Cargo& piece);  // host: the guest's piece to itself

}  // namespace cargo_transfer
