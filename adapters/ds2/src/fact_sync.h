#pragma once
// The host's story, order and progress facts, mirrored to the guests (FACT_SET, fact_wire.h). The host sends each
// fact the game changes during gameplay; a guest writes them into its own fact database, so orders, story flags and
// progress follow the host's world. A guest's own changes are never sent.
#include "net_client.h"

namespace fact_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace fact_sync
