#pragma once
// The network side of the authority table (authority.h): one table for the adapter, the local actions queued for the
// net thread to send (reliable, to all), the peers' messages applied, a leaving peer's objects released, and the local
// claims repeated to a peer that joined or asked for a resync. The first users will be vehicles and enemies.
#include <cstdint>
#include <span>

#include "authority.h"
#include "net_client.h"

namespace authority_sync {

// Local actions, any thread. True when the rules allowed it (the message goes out on the next tick).
bool claim(uint64_t id);
bool assign(uint64_t id, uint8_t owner);  // host only
bool release(uint64_t id);
bool stop(uint64_t id);  // host only
bool decline(uint64_t id);

bool isOwner(uint64_t id);
uint8_t ownerOf(uint64_t id);  // authority_wire::kNoOwner when unowned or unknown
uint8_t pickOwner(uint64_t id, std::span<const uint8_t> candidates);

// Called on whichever thread changes a record, with the table locked: keep it short and never call back in.
void setChangeHandler(authority::AuthorityTable::ChangeHandler handler);

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace authority_sync
