#pragma once
// The host's BT world, mirrored to the guests (BT_ENV and CATCHER_EVENT, bt_wire.h): which regions are BT-active, sent
// once a second and at once on a change, and each catcher activation as it happens. A guest follows the regions and
// replays the activations through the game's own calls while its own BT changes are vetoed (ds2/bt_events.cpp).
#include "net_client.h"

namespace bt_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace bt_sync
