#pragma once
// The host's time of day and weather, mirrored to the guests (WORLD_ENV, env_wire.h). The host sends its clock and
// forecast table once a second and at once when a region's weather or the clock jumps; a guest follows the newest one
// and its own forecast is pinned, so both worlds show the same sky. A guest's own sky is never sent.
#include "net_client.h"

namespace env_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace env_sync
