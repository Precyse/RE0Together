#pragma once
// CARGO_GONE: the game moved a piece of the partner's rack into a terminal or to the local player, so the partner's machine
// deletes its copy (docs/DS2_NOTES.md, "Partner cargo at a terminal"). MOVE_ACK: the receiver of a CARGO_GONE or of a
// give (CARGO_ADD) tells the sender once its own CARGO_LIST shows the move, which ends the sender's hold in rack_sync.
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace partner_cargo_wire {

constexpr uint16_t kMsgCargoGone = proto::kFirstGameType + 0x1A;  // 0x011A, to the partner, reliable: CargoGone
constexpr uint16_t kMsgMoveAck = proto::kFirstGameType + 0x30;    // 0x0130, to the sender, reliable: MoveAck

struct CargoGone {
    uint32_t type;
    uint32_t reserved;
    uint64_t orderId;  // 0 for plain cargo
};
static_assert(sizeof(CargoGone) == 16);

enum class Moved : uint32_t { Gone = 0, Added = 1 };

struct MoveAck {
    uint32_t type;
    Moved moved;       // which move is acknowledged
    uint64_t orderId;  // 0 for plain cargo
};
static_assert(sizeof(MoveAck) == 16);

template <class T>
bool decode(std::span<const uint8_t> payload, T& out) {
    if (payload.size() != sizeof(T)) return false;
    std::memcpy(&out, payload.data(), sizeof(out));
    return true;
}

}  // namespace partner_cargo_wire
