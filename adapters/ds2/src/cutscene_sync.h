#pragma once
// Cutscenes watched together (CUTSCENE_START / READY / GO / END, cutscene_wire.h): the host announces a story Sequence and
// holds it until every guest has its copy held, then releases both; the host's stop is sent so a skip reaches the guests.
#include "net_client.h"

namespace cutscene_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace cutscene_sync
