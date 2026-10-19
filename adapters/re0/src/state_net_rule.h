#pragma once
#include <cstdint>

#include "protocol.h"

// Which message types belong to partner_mode=state (docs/RE0_NOTES.md "State mode: plumbing"). Pure, no game access.
namespace state_net_rule {

// The block every state-mode type lives in, so one guard can refuse all of them outside state mode.
constexpr uint16_t kFirstStateType = proto::kFirstGameType + 0x40;  // 0x0140
constexpr uint16_t kLastStateType = proto::kFirstGameType + 0x7f;   // 0x017f

constexpr bool isStateType(uint16_t type) { return type >= kFirstStateType && type <= kLastStateType; }

// A state-mode type may be sent only in state mode; every other type is not this guard's business.
constexpr bool allowed(bool stateMode, uint16_t type) { return stateMode || !isStateType(type); }

}  // namespace state_net_rule
