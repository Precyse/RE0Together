#pragma once
#include <cstdint>
#include <span>

#include "authority_wire.h"
#include "protocol.h"

// Wire ids of the room authority table (state mode, state block 0x0140..0x017f). The payload and its meaning are DS2's
// `authority_wire::AuthMessage` with the room's scene id as the object id. The table itself speaks DS2's own type
// numbers (0x0111..0x0113, which in RE0 are other messages and never go on the wire); `tableKind` maps at the boundary.
namespace room_authority_wire {

using authority_wire::AuthMessage;

constexpr uint16_t kMsgClaim = proto::kFirstGameType + 0x66;    // 0x0166, reliable, to all: the owner takes the room
constexpr uint16_t kMsgDecline = proto::kFirstGameType + 0x67;  // 0x0167, reliable, to all: the owner cannot host it
constexpr uint16_t kMsgStop = proto::kFirstGameType + 0x68;     // 0x0168, reliable, to all: the owner gives the room up

constexpr uint64_t kSceneIdLimit = 0xaf;  // scene ids are below this (scene.h)
constexpr uint8_t kSlotLimit = 8;         // owner slots the launcher can hand out are below this

constexpr bool isRoomAuthType(uint16_t type) { return type == kMsgClaim || type == kMsgDecline || type == kMsgStop; }

// The AuthorityTable message kind of one of this build's ids; 0 for any other type.
constexpr uint16_t tableKind(uint16_t type) {
    switch (type) {
        case kMsgClaim: return authority_wire::kMsgAuthClaim;
        case kMsgDecline: return authority_wire::kMsgAuthDecline;
        case kMsgStop: return authority_wire::kMsgAuthStop;
        default: return 0;
    }
}

// False for a payload of the wrong size, no owner, epoch 0, a scene id or an owner slot out of range.
inline bool decode(std::span<const uint8_t> payload, AuthMessage& out) {
    return authority_wire::decode(payload, out) && out.id < kSceneIdLimit && out.owner < kSlotLimit;
}

}  // namespace room_authority_wire
