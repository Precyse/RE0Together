#pragma once
#include <cstdint>

#include "sr2_addresses.h"
#include "vehicle_pool_sites.h"

// The pure rules of the vehicle pool move (milestone 15 B3, unit tested; docs/SR2_NOTES.md, "Vehicle pool (milestone 15)"): the
// size of a new block and where an old address of a table lands in it.
namespace vehicle_pool_rules {

// The move is asked for with 3 or more players and the milestone 4 and 9 logic in: max_players 2 installs none of it.
constexpr bool requested(uint32_t maxPlayers, bool crowdLogic) { return maxPlayers > sr2::kCoopDefaultPlayers && crowdLogic; }

using vehicle_pool_sites::kGrownEntries;
using vehicle_pool_sites::kGrownTrailer;
using vehicle_pool_sites::kShippedEntries;
using vehicle_pool_sites::kShippedTrailer;
using vehicle_pool_sites::Table;

constexpr uint32_t kBlockAlignment = 16;

constexpr uint32_t alignedUp(uint32_t bytes) { return (bytes + kBlockAlignment - 1) & ~(kBlockAlignment - 1); }

// The bytes of the shipped array, and of the new array.
constexpr uint32_t shippedArrayBytes(const Table& table) { return kShippedEntries * table.elementSize; }
constexpr uint32_t grownArrayBytes(const Table& table) { return kGrownEntries * table.elementSize; }

// The bytes of a table's new block: the array of 64 entries, then (byte tables) the count byte and a stack of 64 indices,
// rounded up to the block alignment.
constexpr uint32_t blockBytes(const Table& table) {
    return alignedUp(grownArrayBytes(table) + (table.countAndStack ? kGrownTrailer : 0));
}

// Where the old address `value` of a table lands once the table lives at `newBase`: the array keeps its offset, everything from
// the end of the shipped array on (the count byte, the stack and an end-of-array bound) keeps its distance from the end of the
// new array.
constexpr uint32_t relocated(const Table& table, uint32_t newBase, uint32_t value) {
    const uint32_t shippedEnd = table.base + shippedArrayBytes(table);
    return value < shippedEnd ? newBase + (value - table.base) : newBase + grownArrayBytes(table) + (value - shippedEnd);
}

// The new addresses of a byte table's count byte and its stack.
constexpr uint32_t countByteAt(const Table& table, uint32_t newBase) { return newBase + grownArrayBytes(table); }
constexpr uint32_t stackAt(const Table& table, uint32_t newBase) { return countByteAt(table, newBase) + vehicle_pool_sites::kCountByteBytes; }

// The shipped addresses of a byte table's count byte (vehicles of that table in use), for the checks before the move.
constexpr uint32_t shippedCountByteAt(const Table& table) { return table.base + shippedArrayBytes(table); }

}  // namespace vehicle_pool_rules
