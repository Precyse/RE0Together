#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "partner_sites.h"

// Where milestone 15 changes the exe to hold 64 vehicles instead of 32 (docs/SR2_NOTES.md, "Vehicle pool (milestone 15)").
// The static vehicle pool and everything sized one to one with it are `.bss` arrays reached through absolute addresses, so each
// is moved to a new block and every reference to it is rewritten. The layout of each table, the bound immediates and the two
// headroom call blocks are here; the reference rows (the exe's base relocation entries that fall in a table) are generated
// into vehicle_pool_references.h by tools/sr2/vehicle_pool_sites.py. Written all or nothing, only with partner_experimental.
namespace vehicle_pool_sites {

constexpr uint32_t kShippedEntries = 32;
constexpr uint32_t kGrownEntries = 64;

// The tables moved. The byte tables (T1..T8) end in a count byte and a stack of free indices; the others are plain arrays.
enum class TableId : uint8_t {
    Pool,          // VP: the vehicle objects
    T1,
    T2,
    T4,
    T6,
    T7,
    T8,
    Nodes,         // CN: the category nodes of the vehicles
    SlotPointers,  // CU pointers: one customisation slot pointer per vehicle
    BuffersExt,    // CU buffers: the "ext vcust cb" buffers
    BuffersCb,     // CU buffers: the "vcust cb" buffers
    ObjectsCb,     // CU A: the "vcust cb" mempool objects
    ObjectsExt,    // CU B: the "ext vcust cb" mempool objects
};

struct Table {
    TableId id;
    const char* name;
    uint32_t base;            // the shipped array
    uint32_t elementSize;
    bool countAndStack;       // the array is followed by a count byte and a byte stack of free indices
    size_t expectedReferences;  // the research inventory (docs/SR2_NOTES.md), checked against the generated rows
};

// The count byte (vehicles in use) follows the array, then the stack of free indices, one byte per entry.
constexpr uint32_t kCountByteBytes = 1;
constexpr uint32_t kShippedTrailer = kCountByteBytes + kShippedEntries;
constexpr uint32_t kGrownTrailer = kCountByteBytes + kGrownEntries;

inline constexpr Table kTables[] = {
    {TableId::Pool, "VP", 0x2FED938, 0x95A4, false, 2},
    {TableId::T1, "T1", 0x3118DB8, 0x128, true, 28},
    {TableId::T2, "T2", 0x311B2E0, 0x58, true, 33},
    {TableId::T4, "T4", 0x311C218, 0x10, true, 31},
    {TableId::T6, "T6", 0x311C570, 0x7D8, true, 32},
    {TableId::T7, "T7", 0x312C098, 0xE0, true, 27},
    {TableId::T8, "T8", 0x3179F70, 0xF8, true, 25},
    {TableId::Nodes, "CN", 0x215C370, 0x1C, false, 6},
    {TableId::SlotPointers, "CU pointers", 0x223F9F0, 4, false, 4},
    {TableId::BuffersExt, "CU buffers ext", 0x223FA70, 0x1400, false, 1},
    {TableId::BuffersCb, "CU buffers cb", 0x2267A70, 0x1800, false, 1},
    {TableId::ObjectsCb, "CU A", 0x2FAAA00, 0x54, false, 15},
    {TableId::ObjectsExt, "CU B", 0x2FAB480, 0x54, false, 14},
};

constexpr size_t kTableCount = std::size(kTables);
constexpr size_t kExpectedReferences = 219;

constexpr const Table& tableOf(TableId id) { return kTables[static_cast<size_t>(id)]; }

// A bound immediate: the instruction compares an index or a count with the shipped 32 (or 0x1F, or 32 records of 0x54 bytes);
// the new instruction compares with the 64 case. `expected` and `replacement` are the whole instruction.
struct Bound {
    TableId table;
    uintptr_t address;
    std::string_view expected;
    std::string_view replacement;
};

inline constexpr Bound kBounds[] = {
    {TableId::T1, 0xAE5075, "3C 20", "3C 40"},
    {TableId::T1, 0xAE57E7, "3C 20", "3C 40"},
    {TableId::T1, 0xAE57F5, "3C 20", "3C 40"},
    {TableId::T1, 0xAE218D, "3C 20", "3C 40"},
    {TableId::T1, 0xAE5545, "6A 20", "6A 40"},
    {TableId::T2, 0xADDCC5, "3C 20", "3C 40"},
    {TableId::T2, 0xAE50F5, "3C 20", "3C 40"},
    {TableId::T2, 0xAE5887, "3C 20", "3C 40"},
    {TableId::T2, 0xAE5895, "3C 20", "3C 40"},
    {TableId::T2, 0xAE21A7, "3C 20", "3C 40"},
    {TableId::T2, 0xAE5565, "6A 20", "6A 40"},
    {TableId::T4, 0xAE51F5, "3C 20", "3C 40"},
    {TableId::T4, 0xAE5987, "3C 20", "3C 40"},
    {TableId::T4, 0xAE5995, "3C 20", "3C 40"},
    {TableId::T4, 0xAE21D7, "3C 20", "3C 40"},
    {TableId::T6, 0xAB04DE, "3C 20", "3C 40"},
    {TableId::T6, 0xAE52F5, "3C 20", "3C 40"},
    {TableId::T6, 0xAE5A97, "3C 20", "3C 40"},
    {TableId::T6, 0xAE5AA5, "3C 20", "3C 40"},
    {TableId::T6, 0xAE2207, "3C 20", "3C 40"},
    {TableId::T7, 0xAE5375, "3C 20", "3C 40"},
    {TableId::T7, 0xAE5B37, "3C 20", "3C 40"},
    {TableId::T7, 0xAE5B45, "3C 20", "3C 40"},
    {TableId::T7, 0xAE221F, "3C 20", "3C 40"},
    {TableId::T8, 0xB6C097, "3C 20", "3C 40"},
    {TableId::T8, 0xB6C0A6, "3C 20", "3C 40"},
    {TableId::T8, 0xB6D167, "3C 20", "3C 40"},
    {TableId::T8, 0xB6D175, "3C 20", "3C 40"},
    {TableId::T8, 0xAE2247, "3C 20", "3C 40"},
    {TableId::Nodes, 0x9287B1, "C7 01 20 00 00 00", "C7 01 40 00 00 00"},
    {TableId::Nodes, 0x92921A, "BA 20 00 00 00", "BA 40 00 00 00"},
    {TableId::SlotPointers, 0xAC3323, "83 F8 1F", "83 F8 3F"},
    {TableId::SlotPointers, 0xAC5EA3, "83 F9 20", "83 F9 40"},
    {TableId::ObjectsCb, 0xAC5C4D, "81 FE 80 0A 00 00", "81 FE 00 15 00 00"},
};

constexpr size_t kExpectedBounds = 34;

// The two spawners of parked vehicles test `0x20 - vehicles in use` (the pool size) in a block of their own: each becomes a call
// to a thunk that answers with the headroom of the new pool, the result in the register the block loaded.
inline constexpr partner_sites::Site kHeadroomBlocks[] = {
    {"HD1", partner_sites::Kind::CallBlock, partner_sites::Gate::Now, 0x94F9F0, "B9 20 00 00 00 2B 0D D4 A0 52 02",
     partner_sites::Thunk::VehicleHeadroomEcx, "90 90 90 90 90 90"},
    {"HD2", partner_sites::Kind::CallBlock, partner_sites::Gate::Now, 0x94FD69, "B8 20 00 00 00 2B 05 D4 A0 52 02",
     partner_sites::Thunk::VehicleHeadroomEax, "90 90 90 90 90 90"},
};

// The customisation allocators sized for 32 vehicles (docs/SR2_NOTES.md): the count each holds now and with 64 vehicles, the
// element size and the offsets of its free list links.
struct CustomAllocator {
    const char* name;
    uintptr_t address;
    uint32_t shippedCount;
    uint32_t grownCount;
    uint32_t elementSize;
    uint32_t nextOffset;
    uint32_t previousOffset;
};

inline constexpr CustomAllocator kCustomAllocators[] = {
    {"custom instance", 0x2FAA890, 32, 64, 0x84, 0x7C, 0x80},
    {"custom component", 0x2FAA8E0, 32 * 50, 64 * 50, 0x18, 0x10, 0x14},
    {"custom color", 0x2FAA930, 32 * 100, 64 * 100, 0x18, 0x10, 0x14},
};

}  // namespace vehicle_pool_sites
