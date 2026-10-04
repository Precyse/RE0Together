#pragma once
// The host's enemy camps' alert phase, mirrored to the guests (CAMP_ALERT, camp_wire.h).
#include "net_client.h"

namespace camp_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace camp_sync
