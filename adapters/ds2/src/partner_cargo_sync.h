#pragma once
// A piece the game moved between the partner's rack (the body's owner) and this world is mirrored on the partner's
// machine: one that went into a terminal or to the local player is deleted there (CARGO_GONE, partner_cargo_wire.h), one
// the local player put into the partner's rack is created there with its order link (CARGO_ADD). The partner's CARGO_LIST
// follows a moment later, so the pieces moved lately are remembered for rack_sync, which must not recreate a piece that left
// or delete one that arrived before the list shows it.
#include <vector>

#include "game.h"
#include "net_client.h"

namespace partner_cargo_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// Net thread: the pieces the partner's rack lost to this world, and the ones this world gave it, in the last few seconds.
std::vector<game::Cargo> recentlyLeft();
std::vector<game::Cargo> recentlyGiven();

}  // namespace partner_cargo_sync
