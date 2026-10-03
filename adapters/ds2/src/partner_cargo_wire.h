#pragma once
// CARGO_GONE: the host's game moved a piece of the partner's rack into a terminal (delivered), so the partner's machine
// deletes its copy (docs/DS2_NOTES.md, "Partner cargo at a terminal").
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace partner_cargo_wire {

constexpr uint16_t kMsgCargoGone = proto::kFirstGameType + 0x1A;  // 0x011A, host to the guest, reliable: CargoGone

struct CargoGone {
    uint32_t type;
    uint32_t reserved;
    uint64_t orderId;  // 0 for plain cargo
};
static_assert(sizeof(CargoGone) == 16);

inline bool decode(std::span<const uint8_t> payload, CargoGone& out) {
    if (payload.size() != sizeof(CargoGone)) return false;
    std::memcpy(&out, payload.data(), sizeof(out));
    return true;
}

}  // namespace partner_cargo_wire
