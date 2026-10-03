#pragma once
// A piece of the partner's rack that the host's game delivered at a terminal is deleted on the partner's machine too
// (CARGO_GONE, partner_cargo_wire.h).
#include "net_client.h"

namespace partner_cargo_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace partner_cargo_sync
