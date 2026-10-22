#pragma once

// Switches co-op from the game's 1-peer deltacomp replication to its 8-peer one, so up to 8 players are replicated.
// Everything is applied together on the first co-op entry, or not at all.
namespace replication_switch {

// Start-up check of every site that enable() will patch; logs each difference. True when all match.
bool sitesMatch();

// Called before net_set_mode runs its co-op case, once the co-op pools are resized. Verifies the sites and the eight
// N=8 allocator objects, then jumps every N=1 deltacomp entry to its N=8 twin, makes co-op index the arrays by the
// member's session slot, names the co-op allocator set "deltacomp_sa" and points the N=8 allocators at the co-op pool.
// Does nothing after the first success; logs and leaves the game untouched when anything differs. With `vehiclePool` (the 64
// vehicle pool is in) the vehicle set also holds 960 elements instead of 480.
void enable(bool vehiclePool);

// Puts the peer index back to 0 (the N=8 code then serves one peer, like the N=1 code did). For a member whose
// session slot would index the arrays out of range.
void fallBackToSinglePeer();

}  // namespace replication_switch
