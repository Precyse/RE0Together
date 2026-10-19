#pragma once
#include <cstdint>

#include "frame_router_rule.h"
#include "net_client.h"

// Receive side of partner_mode=state: a state-mode message type registers its handler here together with who may send
// it. Registered once at enable time, before state_sync::start; dispatched on the net thread.
namespace frame_router {

using Handler = void (*)(const GameFrame& frame);
using frame_router_rule::SenderPolicy;

// False when the type is outside the state block or already registered.
bool add(uint16_t type, SenderPolicy policy, Handler handler);

// True when `frame.type` is registered (the frame is consumed: handed to its handler, or dropped and counted when the
// sender policy refuses it). False leaves the frame to the caller. `hostSlot` is the session's host.
bool dispatch(const GameFrame& frame, uint8_t hostSlot);

}  // namespace frame_router
