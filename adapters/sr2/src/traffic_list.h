#pragma once
#include <cstddef>
#include <cstdint>

#include "population_rules.h"

// The ambient traffic as the game keeps it (docs/SR2_NOTES.md, "Traffic and the vehicle pool"): one circular list of the vehicles
// of every machine's traffic, with the owner of each. The traffic quota (population.cpp) and the traffic trace (traffic_trace.cpp)
// read it.
namespace traffic_list {

struct Vehicle {
    uintptr_t object;
    uint8_t ownerSlot;  // the net record's owner, this machine's slot for a vehicle with no record
    bool owned;         // this machine drives it and may despawn it (the game's own test, 0x938D80)
    population_rules::Point position;
};

// The owner slot of this machine's session member, false outside a session.
bool localSlot(uint8_t& slot);

// The players with a human, the local one first, then the remote ones in ascending owner slot; the local slot (0 outside a
// session). At most `capacity` players. The positions are the humans' (float reads, no float math).
size_t players(population_rules::Player* out, size_t capacity, uint8_t& localSlot);

// The vehicles of the traffic list (at most `capacity`, never more than the walk limit), true to the list order.
size_t take(Vehicle* out, size_t capacity);

// How many of them this machine owns.
size_t ownedCount();

}  // namespace traffic_list
