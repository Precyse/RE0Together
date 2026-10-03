#pragma once
// The host's placed and removed structures, mirrored to the guests (STRUCT_CREATE / STRUCT_REMOVE, struct_wire.h).
// The host sends what its player places or removes; a guest rebuilds or removes the same structure under the host's
// construction id, and its own placements are refused.
#include "net_client.h"

namespace struct_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace struct_sync
