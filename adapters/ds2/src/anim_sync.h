#pragma once
// The partner's animation: each machine reports the changes of its own player's animation variables (ANIM_STATE, at
// 30 Hz when something changed, and a full snapshot every second so a lost report heals), and the other machine writes
// them into its remote body (ds2/remote_animation) before the graph evaluates it. Variable indices are the same on
// both machines (the same entity resource), so a report is just index, type and value.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace anim_sync {

constexpr uint16_t kMsgAnimState = proto::kFirstGameType + 10;  // 0x010A, to all, unreliable: AnimHeader, then entries

// Payload: AnimHeader, then `count` entries {u16 index, u8 type, value}; the value is 1 byte (bool), 4 bytes (int,
// float) or 16 bytes (quat) by the type (the engine's variable types 0, 1, 2, 3).
struct AnimHeader {
    uint32_t seq;
    uint16_t count;
    uint16_t flags;  // kFlagSnapshot
};
static_assert(sizeof(AnimHeader) == 8);
constexpr uint16_t kFlagSnapshot = 1;

// Net thread: the partner's reports, and sending the local player's changes.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace anim_sync
