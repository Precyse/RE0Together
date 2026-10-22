#pragma once
#include <cstdint>

#include "pad_frame.h"
#include "protocol.h"

// Which message types belong to partner_mode=state (docs/RE0_NOTES.md "State mode: plumbing"). Pure, no game access.
namespace state_net_rule {

// The block every state-mode type lives in, so one guard can refuse all of them outside state mode.
constexpr uint16_t kFirstStateType = proto::kFirstGameType + 0x40;  // 0x0140
constexpr uint16_t kLastStateType = proto::kFirstGameType + 0x7f;   // 0x017f

constexpr bool isStateType(uint16_t type) { return type >= kFirstStateType && type <= kLastStateType; }

// A state-mode type may be sent only in state mode; every other type is not this guard's business.
constexpr bool allowed(bool stateMode, uint16_t type) { return stateMode || !isStateType(type); }

// A received type that only the other mode produces: a state-block type comes from a state-mode peer, PAD_FRAME (never
// sent in state mode) from an input-mode one. Nothing negotiates partner_mode, so this is how a mismatch shows.
constexpr bool peerRunsOtherMode(bool stateMode, uint16_t type) {
    return stateMode ? type == pad::kMsgPadFrame : isStateType(type);
}

}  // namespace state_net_rule
