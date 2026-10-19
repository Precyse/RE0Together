#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"
#include "vehicle_pool_sites.h"

// Milestone 15 B3: the static vehicle pool of 32 and everything sized one to one with it, moved to blocks of 64 (docs/SR2_NOTES.md,
// "Vehicle pool (milestone 15)"; the site tables are vehicle_pool_sites.h and the generated vehicle_pool_references.h). Written
// all or nothing, only with partner_experimental, 3 or more players and the partner logic. The move happens at the first co-op
// entry, when the game is on its front end: before it every table is checked to be empty and the world never initialised.
namespace vehicle_pool {

// At start-up: checks every reference, bound and block of the move against the exe. True (and the move is wanted) when all match.
bool request();

// True once request() said yes: coop_allocators then gets room for the 64 vehicles' replication state.
bool wanted();

// The move, at the first co-op entry (before the original net_set_mode runs). True when it was written; false (and logged) when
// the state is not the empty front end the move needs, or a table could not be allocated.
bool install();
bool installed();

// How many vehicles the pool holds now: 64 once moved, else the shipped 32.
uint32_t capacity();

// The addresses the census reads: the count byte of a byte table, the first customisation slot pointer (they move with the table).
uintptr_t countByteAddress(vehicle_pool_sites::TableId table);
uintptr_t slotPointersAddress();

// The limits that follow the pool: the allocator-set activation (the limit thunk) raises the class 1 category limit to
// 6 per player and the vehicle creation records to 64; any other mode puts the shipped values back.
void onCoopActivation(uint32_t maxPlayers);
void onModeSet(int mode);

// trace=1: a byte table that is full while the pool is not says that some table of the move is not the only 32 of its kind.
void setTrace(bool trace);
void traceGuard();

// The code behind the headroom call blocks HD1 and HD2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace vehicle_pool
