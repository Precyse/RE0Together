#pragma once
// Authority messages: who simulates one shared object (a vehicle, an enemy, a piece of loose cargo). All three are
// reliable, to all, and carry the same payload; what they mean depends on the type.
//   CLAIM    `owner` owns `id` from now on (epoch = the object's new generation). Sent by the owner itself, or by the
//            host assigning an owner.
//   DECLINE  `owner` cannot host `id` (not loaded here, body not ready): it is barred from owning it until the object
//            is released voluntarily. Sent by `owner`.
//   STOP     `owner` must stop acting on `id`. Sent by `owner` itself (a release) or by the host.
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace authority_wire {

constexpr uint16_t kMsgAuthClaim = proto::kFirstGameType + 0x11;    // 0x0111
constexpr uint16_t kMsgAuthDecline = proto::kFirstGameType + 0x12;  // 0x0112
constexpr uint16_t kMsgAuthStop = proto::kFirstGameType + 0x13;     // 0x0113

constexpr uint8_t kNoOwner = 0xFF;

struct AuthMessage {
    uint64_t id;      // the game's id of the object, the same on every machine
    uint32_t epoch;   // generation of the object's authority record; starts at 1, every change adds one
    uint8_t owner;    // the slot the message is about
    uint8_t reserved[3];
};
static_assert(sizeof(AuthMessage) == 16);

inline bool isAuthType(uint16_t type) {
    return type == kMsgAuthClaim || type == kMsgAuthDecline || type == kMsgAuthStop;
}

// False for a payload of the wrong size or a message about no one (owner 0xFF) or generation 0.
inline bool decode(std::span<const uint8_t> payload, AuthMessage& out) {
    if (payload.size() != sizeof(AuthMessage)) return false;
    std::memcpy(&out, payload.data(), sizeof(out));
    return out.owner != kNoOwner && out.epoch != 0;
}

}  // namespace authority_wire
