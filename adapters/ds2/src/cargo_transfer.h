#pragma once
// Cargo moved between the two players' racks, decided by the host. Each machine keeps its own Sam and cargo; a
// transfer deletes the piece on the giver's machine and creates the same kind on the receiver's with the game's own
// requests (game::removeCargo / addCargo; an order piece is recreated with its order link). Every player reports what its backpack holds (the host's menu lists the
// guest's, each player draws the partner's on their body); either player gives and takes through cargo_menu. The host
// decides: its own moves are carried out at once, a guest's go to it as CARGO_ASK and the host answers with the same
// give or take it would have made itself, and the guest carries out the host's takes.
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
constexpr uint16_t kMsgCargoAsk = proto::kFirstGameType + 0x31;  // 0x0131, guest to host, reliable: CargoAsk
constexpr uint16_t kMsgCargoGive = proto::kFirstGameType + 0x33;  // 0x0133, guest to host, reliable: CargoAdd

constexpr size_t kNameBytes = 44;
constexpr uint32_t kMaxListed = 64;

// One carried piece on the wire. A CARGO_LIST payload is a u32 count followed by that many entries. The order link
// (DSBaggage +0x28 / +0x30) travels with the piece so the partner's body can hold a piece that counts for the same order.
struct CargoEntry {
    uint64_t handle;
    uint32_t type;
    char name[kNameBytes];  // UTF-8, zero padded, cut at kNameBytes
    uint64_t orderId;
    uint64_t secondId;
    float durability;
    uint8_t category;
    uint8_t reserved[3];
};
static_assert(sizeof(CargoEntry) == 80);

// The host asks the guest for one of its pieces; the guest deletes it and answers with CARGO_ADD.
struct CargoTake {
    uint64_t handle;
};
static_assert(sizeof(CargoTake) == 8);

// A guest's request to the host: wantsIt = 1 asks the host to give its piece `handle`, 0 offers the guest's own piece
// `handle` (the host then takes it with CARGO_TAKE). The host ignores a handle it does not list.
struct CargoAsk {
    uint64_t handle;
    uint8_t wantsIt;
    uint8_t reserved[7];
};
static_assert(sizeof(CargoAsk) == 16);

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

// False for a payload that is not exactly one CargoAdd (CARGO_ADD and CARGO_GIVE).
inline bool decodeAdd(std::span<const uint8_t> payload, CargoAdd& out) {
    if (payload.size() != sizeof(CargoAdd)) return false;
    std::memcpy(&out, payload.data(), sizeof(out));
    return true;
}

struct Partner {
    uint8_t slot = 0;
    std::vector<game::Cargo> cargo;
};

// Net thread: asks `slot` to create `piece` (kind and order link) on its own player. False when the send failed.
bool sendPiece(NetClient& net, uint8_t slot, const game::Cargo& piece);

// Net thread, guest: reports `piece`, which the game moved into the host's rack, as CARGO_GIVE. The caller holds it on the body.
void offerPiece(NetClient& net, uint8_t hostSlot, const game::Cargo& piece);

// Net thread, host: sends `piece` to the guest in `slot` and, once that is sent, deletes the local copy. False (nothing deleted)
// when the send fails or the piece is gone.
bool handOver(NetClient& net, uint8_t slot, const game::Cargo& piece);

// Net thread: messages from the partner, and the periodic work (refresh, report, queued transfers).
void onFrame(NetClient& net, const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// Render thread (cargo_menu).
std::vector<game::Cargo> localCargo();
std::optional<Partner> partner();  // the partner and what it reported carrying (the menu's right column)
void give(const game::Cargo& piece);  // the local player's piece to the partner (a guest asks the host)
void take(const game::Cargo& piece);  // the partner's piece to the local player (a guest asks the host)

}  // namespace cargo_transfer
