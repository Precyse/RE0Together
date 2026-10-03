#pragma once
// The partner's rack on the partner's body. Each player already reports the cargo its backpack holds (CARGO_LIST); the
// body gets the same kinds created in the main load of its own backpack owner (the remote has one of its own: see
// ds2/remote_baggage.cpp), so it carries the partner's real pieces with their real models. The pieces belong only to the
// body's owner, never to the local inventory: every write is refused when the owner is part of the local player's tree.
#include "net_client.h"

namespace rack_sync {

// Net thread.
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace rack_sync
