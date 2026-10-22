#pragma once
// The host's voidout craters, mirrored to the guests (CRATER_SET, crater_wire.h): the whole set on a change, every few
// seconds, and at once to a peer that just joined. A guest follows it through the game's own AddCrater and RemoveCrater
// (ds2/craters.cpp).
#include "net_client.h"

namespace crater_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace crater_sync
