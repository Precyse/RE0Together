// The vehicle pool move of milestone 15 B3 (no game): the reference table against the research inventory, the address math of the
// move, the bound immediates and headroom blocks as data, their clearance from every other site and from Juiced, and the stores
// that bring the new tables to the state the game's constructors leave the shipped ones in.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../src/partner_replacement.h"
#include "../src/vehicle_pool_init.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_rules.h"
#include "../src/vehicle_pool_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using vehicle_pool_references::kReferences;
using vehicle_pool_sites::kBounds;
using vehicle_pool_sites::kHeadroomBlocks;
using vehicle_pool_sites::kTables;
using vehicle_pool_sites::Table;
using vehicle_pool_sites::TableId;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kDwordBytes = 4;
constexpr uint32_t kFakeBase = 0x20000000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

size_t referencesOf(TableId id) {
    return static_cast<size_t>(std::count_if(std::begin(kReferences), std::end(kReferences), [id](const auto& row) { return row.table == id; }));
}

// The shipped extent of a table that a reference may point into: the array, and for a byte table the count byte and the stack.
uint32_t shippedExtentEnd(const Table& table) {
    return table.base + vehicle_pool_rules::shippedArrayBytes(table) + (table.countAndStack ? vehicle_pool_sites::kShippedTrailer : 0);
}

void theReferenceTableMatchesTheInventory() {
    check(std::size(kReferences) == vehicle_pool_sites::kExpectedReferences && vehicle_pool_sites::kExpectedReferences == 219,
          "219 references in all, as the research counted");
    bool perTable = true;
    size_t sum = 0;
    for (const Table& table : kTables) {
        const size_t found = referencesOf(table.id);
        sum += table.expectedReferences;
        if (found != table.expectedReferences) {
            std::printf("     %s: %zu references, the inventory has %zu\n", table.name, found, table.expectedReferences);
            perTable = false;
        }
    }
    check(perTable && sum == vehicle_pool_sites::kExpectedReferences,
          "per table: VP 2, T1 28, T2 33, T4 31, T6 32, T7 27, T8 25, CN 6, CU pointers 4, CU buffers 2, CU A 15, CU B 14");
    check(referencesOf(TableId::BuffersExt) + referencesOf(TableId::BuffersCb) == 2, "CU buffers: one reference to each of the two buffer blocks");
    check(vehicle_pool_sites::kTableCount == 13 && kTables[static_cast<size_t>(TableId::ObjectsExt)].id == TableId::ObjectsExt &&
              kTables[static_cast<size_t>(TableId::Pool)].id == TableId::Pool,
          "the table list is indexed by the table id");

    bool inside = true;
    for (const auto& row : kReferences) {
        const Table& table = vehicle_pool_sites::tableOf(row.table);
        const uint32_t end = shippedExtentEnd(table) + (table.countAndStack ? 1 : 0);
        const bool poolEnd = row.table == TableId::Pool && row.value == table.base + vehicle_pool_rules::shippedArrayBytes(table);
        if (row.value < table.base || (row.value >= end && !poolEnd)) {
            std::printf("     0x%X holds 0x%X, outside %s\n", static_cast<unsigned>(row.site), row.value, table.name);
            inside = false;
        }
    }
    check(inside, "every reference holds an address inside its own table");

    std::set<uintptr_t> sites;
    for (const auto& row : kReferences) sites.insert(row.site);
    check(sites.size() == std::size(kReferences), "no site twice");
    const auto poolEnd = std::find_if(std::begin(kReferences), std::end(kReferences), [](const auto& row) { return row.site == 0xAE233E; });
    check(poolEnd != std::end(kReferences) && poolEnd->table == TableId::Pool && poolEnd->value == 0x3118DB8,
          "the one reference to the end of the pool (0xAE233E) is the pool's, though 0x3118DB8 is also the base of T1");
    const auto boundary = std::find_if(std::begin(kReferences), std::end(kReferences), [](const auto& row) { return row.site == 0xAC5EB8; });
    check(boundary != std::end(kReferences) && boundary->table == TableId::ObjectsExt && boundary->value == 0x2FAB480,
          "the boundary of the two customisation arrays (0x2FAB480 at 0xAC5EB8) is the base of B");
    bool noConstructors = true;
    for (const uintptr_t constructor : {0xDB2133, 0xDB216A, 0xDB21D6, 0xAE1C70, 0xDB1F73, 0xDB1FA3, 0xDB67A1}) noConstructors = noConstructors && !sites.count(constructor);
    check(noConstructors, "the static constructors and destructors of the shipped arrays are not rewritten");
}

void theAddressMath() {
    const Table& t1 = vehicle_pool_sites::tableOf(TableId::T1);
    using vehicle_pool_rules::relocated;
    check(relocated(t1, kFakeBase, t1.base) == kFakeBase && relocated(t1, kFakeBase, t1.base + 3 * t1.elementSize + 8) == kFakeBase + 3 * t1.elementSize + 8,
          "an address in the array keeps its offset");
    const uint32_t countByte = t1.base + 32 * t1.elementSize;
    check(relocated(t1, kFakeBase, countByte) == kFakeBase + 64 * t1.elementSize && relocated(t1, kFakeBase, countByte) == vehicle_pool_rules::countByteAt(t1, kFakeBase),
          "the count byte moves to the end of the 64 entry array");
    check(relocated(t1, kFakeBase, countByte + 1) == vehicle_pool_rules::stackAt(t1, kFakeBase) && relocated(t1, kFakeBase, countByte + 5) == vehicle_pool_rules::stackAt(t1, kFakeBase) + 4,
          "and the stack follows it, indices kept");
    const Table& pool = vehicle_pool_sites::tableOf(TableId::Pool);
    check(relocated(pool, kFakeBase, pool.base + 32 * pool.elementSize) == kFakeBase + 64 * pool.elementSize, "the end of the pool becomes the end of 64 vehicles");
    check(relocated(pool, kFakeBase, pool.base + 0x84DC) == kFakeBase + 0x84DC, "a field of the first vehicle keeps its offset");
    const Table& nodes = vehicle_pool_sites::tableOf(TableId::Nodes);
    check(relocated(nodes, kFakeBase, nodes.base) == kFakeBase, "the category nodes start at the new block");

    check(vehicle_pool_rules::blockBytes(t1) == vehicle_pool_rules::alignedUp(64 * 0x128 + 65) && vehicle_pool_rules::blockBytes(t1) % 16 == 0,
          "a byte table's block holds 64 entries, the count byte and 64 stack bytes, 16 byte aligned");
    check(vehicle_pool_rules::blockBytes(pool) == 64 * 0x95A4 && vehicle_pool_rules::blockBytes(pool) == 2451712, "the pool: 2,451,712 bytes for 64 vehicles");
    bool fits = true;
    for (const auto& row : kReferences) {
        const Table& table = vehicle_pool_sites::tableOf(row.table);
        const uint32_t moved = relocated(table, kFakeBase, row.value);
        fits = fits && moved >= kFakeBase && moved <= kFakeBase + vehicle_pool_rules::blockBytes(table);
    }
    check(fits, "every reference lands inside its table's new block");
    check(vehicle_pool_rules::requested(4, true, true) && !vehicle_pool_rules::requested(2, true, true) && !vehicle_pool_rules::requested(4, false, true) &&
              !vehicle_pool_rules::requested(4, true, false),
          "the move is asked for only with 3 or more players, partner_experimental and the partner logic: max_players 2 installs none of it");
}

void theBoundsAndTheHeadroomBlocks() {
    check(std::size(kBounds) == vehicle_pool_sites::kExpectedBounds && vehicle_pool_sites::kExpectedBounds == 34, "34 bound immediates");
    std::map<TableId, size_t> perTable;
    bool twice = true;
    for (const auto& bound : kBounds) {
        ++perTable[bound.table];
        const auto before = partner_sites::parseHex(bound.expected);
        const auto after = partner_sites::parseHex(bound.replacement);
        // The immediate is the end of the instruction; every other byte stays.
        uint32_t immediateBefore = 0;
        uint32_t immediateAfter = 0;
        const size_t immediateBytes = before.size() >= 6 ? 4 : before.size() == 2 ? 1 : before.size() == 3 ? 1 : 4;
        for (size_t i = 0; i < immediateBytes; ++i) {
            immediateBefore |= static_cast<uint32_t>(before[before.size() - immediateBytes + i]) << (8 * i);
            immediateAfter |= static_cast<uint32_t>(after[after.size() - immediateBytes + i]) << (8 * i);
        }
        const bool sameOpcode = before.size() == after.size() && std::equal(before.begin(), before.end() - immediateBytes, after.begin());
        const bool grown = immediateBefore == 0x20 ? immediateAfter == 0x40 : immediateBefore == 0x1F ? immediateAfter == 0x3F : immediateBefore == 0xA80 && immediateAfter == 0x1500;
        twice = twice && sameOpcode && grown;
    }
    check(twice, "each bound keeps its opcode and doubles its immediate: 0x20 to 0x40, 0x1F to 0x3F, 32 records of 0x54 to 64");
    check(perTable[TableId::T1] == 5 && perTable[TableId::T2] == 6 && perTable[TableId::T4] == 4 && perTable[TableId::T6] == 5 &&
              perTable[TableId::T7] == 4 && perTable[TableId::T8] == 5 && perTable[TableId::Nodes] == 2 &&
              perTable[TableId::SlotPointers] + perTable[TableId::ObjectsCb] == 3,
          "bounds per group: T1 5, T2 6, T4 4, T6 5, T7 4, T8 5, CN 2, CU 3");

    check(std::size(kHeadroomBlocks) == 2 && kHeadroomBlocks[0].address == 0x94F9F0 && kHeadroomBlocks[1].address == 0x94FD69 &&
              partner_sites::parseHex(kHeadroomBlocks[0].expected) == std::vector<uint8_t>{0xB9, 0x20, 0, 0, 0, 0x2B, 0x0D, 0xD4, 0xA0, 0x52, 0x02} &&
              partner_sites::parseHex(kHeadroomBlocks[1].expected) == std::vector<uint8_t>{0xB8, 0x20, 0, 0, 0, 0x2B, 0x05, 0xD4, 0xA0, 0x52, 0x02},
          "HD1 and HD2: mov ecx/eax, 0x20; sub it, [0x252A0D4]");
    bool shapes = true;
    for (const auto& site : kHeadroomBlocks) {
        const auto written = partner_replacement::replacementFor(site, 0x10004000);
        shapes = shapes && site.kind == partner_sites::Kind::CallBlock && written.size() == partner_sites::parseHex(site.expected).size() && written[0] == 0xE8;
    }
    check(shapes, "each headroom block becomes a call to its thunk and nops over the rest, the same length");
    check(kHeadroomBlocks[0].thunk != kHeadroomBlocks[1].thunk, "HD1 and HD2 have a thunk each (ecx and eax)");
}

struct Range {
    uintptr_t address;
    size_t size;
    std::string name;
};

std::vector<Range> everyWrittenRange() {
    std::vector<Range> ranges;
    for (const auto& row : kReferences) ranges.push_back({row.site, kDwordBytes, "reference"});
    for (const auto& bound : kBounds) ranges.push_back({bound.address, partner_sites::parseHex(bound.expected).size(), "bound"});
    for (const auto& site : kHeadroomBlocks) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size(), site.id});
    return ranges;
}

void theClearance() {
    const std::vector<Range> ranges = everyWrittenRange();
    bool apart = true;
    for (size_t a = 0; a < ranges.size(); ++a) {
        for (size_t b = a + 1; b < ranges.size(); ++b) {
            if (other_sites::overlaps(ranges[a].address, ranges[a].size, ranges[b].address, ranges[b].size)) {
                std::printf("     0x%X and 0x%X overlap\n", static_cast<unsigned>(ranges[a].address), static_cast<unsigned>(ranges[b].address));
                apart = false;
            }
        }
    }
    check(apart, "no two writes of the move overlap");

    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), activities.begin(), activities.end());
    others.insert(others.end(), crowd.begin(), crowd.end());
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const partner_sites::Site& site : table) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    bool clear = true;
    for (const Range& range : ranges) {
        if (other_sites::anyOverlap(others, range.address, range.size)) {
            std::printf("     %s at 0x%X overlaps a site of an earlier milestone\n", range.name.c_str(), static_cast<unsigned>(range.address));
            clear = false;
        }
    }
    check(clear, "no write of the move overlaps a site of milestones 3 to 13");

    bool clearOfJuiced = true;
    for (const Range& range : ranges) {
        const uintptr_t first = range.address - kJuicedMargin;
        const uintptr_t last = range.address + range.size + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", range.name.c_str(), static_cast<unsigned>(range.address), static_cast<unsigned>(kJuicedMargin));
            clearOfJuiced = false;
        }
    }
    check(clearOfJuiced, "no write of the move within 16 bytes of an address Juiced touches");
}

// A heap block as a table's memory; the games addresses are 32 bit, so is every pointer here.
struct Block {
    explicit Block(size_t bytes) : memory(bytes + 16, 0) {}
    uintptr_t address() { return reinterpret_cast<uintptr_t>(memory.data()); }
    std::vector<uint8_t> memory;
};

int g_vehicleConstructed = 0;
int g_mempoolConstructed = 0;
int g_vectorConstructed = 0;

const vehicle_pool_init::Constructors kStubs{[](uintptr_t) { ++g_vehicleConstructed; }, [](uintptr_t) { ++g_mempoolConstructed; },
                                              [](uintptr_t) { ++g_vectorConstructed; }};

void theConstructors() {
    using namespace vehicle_pool_init;
    Block t1(64 * 0x128);
    construct(TableId::T1, t1.address(), kStubs);
    bool elements = true;
    for (uint32_t i = 0; i < 64; ++i) {
        const uintptr_t e = t1.address() + i * 0x128;
        elements = elements && load(e + 0xC) == 0xFF && load(e + 0x18) == 0xFF && load(e + 0x30) == 0xFF && load(e + 0x10) == kMinusOne &&
                   load(e + 0x14) == kMinusOne && load(e + 0x1C) == kMinusOne && load(e + 0x20) == kMinusOne && load(e + 0x34) == kMinusOne &&
                   load(e + 0x38) == kMinusOne && load(e + 0x11C) == kMinusOne && load(e + 0x120) == kMinusOne && load(e + 0) == 0 && load(e + 0x3C) == 0;
    }
    check(elements && g_vectorConstructed == 64, "T1: all 64 elements get the markers and the minus ones of 0xDB2160, and the vector constructor runs for each");

    Block t2(64 * 0x58);
    construct(TableId::T2, t2.address(), kStubs);
    bool links = true;
    for (uint32_t i = 0; i < 64; ++i) {
        const uintptr_t e = t2.address() + i * 0x58;
        links = links && load(e + 4) == kMinusOne && load(e + 8) == e + 0x14 && load(e + 0xC) == 0 && load(e + 0x10) == 0x80000008;
    }
    check(links, "T2: all 64 elements get the four stores of 0xDB21D0 (+4 = -1, +8 = the element's own +0x14, +0x10 = 0x80000008)");

    Block pool(64 * 0x95A4);
    construct(TableId::Pool, pool.address(), kStubs);
    Block objects(64 * 0x54);
    construct(TableId::ObjectsCb, objects.address(), kStubs);
    check(g_vehicleConstructed == 64 && g_mempoolConstructed == 64, "the pool constructs 64 vehicles and a customisation array 64 mempool objects");
    Block untouched(64 * 4);
    construct(TableId::SlotPointers, untouched.address(), kStubs);
    check(g_vehicleConstructed == 64 && g_mempoolConstructed == 64 && load(untouched.address()) == 0, "the other tables are left zero");
}

void theByteTableReset() {
    using namespace vehicle_pool_init;
    Block table(64 * 0x10 + 65);
    uint8_t* count = reinterpret_cast<uint8_t*>(table.address() + 64 * 0x10);
    *count = 9;
    resetByteTable(reinterpret_cast<uintptr_t>(count), 64);
    bool stack = *count == 0;
    for (uint32_t i = 0; i < 64; ++i) stack = stack && count[1 + i] == i;
    check(stack, "a byte table resets to a count of 0 and a stack of the indices 0..63");
}

void theFreeLists() {
    using namespace vehicle_pool_init;
    Block elements(3 * 0x18);
    uint32_t head = 0;
    const uintptr_t first = elements.address();
    const uintptr_t second = first + 0x18;
    const uintptr_t third = second + 0x18;
    pushFree(head, first, 0x10, 0x14);
    check(head == first && load(first + 0x10) == first && load(first + 0x14) == first, "the first element links to itself and is the head");
    pushFree(head, second, 0x10, 0x14);
    pushFree(head, third, 0x10, 0x14);
    check(head == third, "each new element becomes the head");
    bool circle = load(third + 0x10) == second && load(second + 0x10) == first && load(first + 0x10) == third && load(third + 0x14) == first &&
                  load(first + 0x14) == second && load(second + 0x14) == third;
    check(circle, "the list is circular both ways, as 0xACAE00 builds it");

    Block nodes(64 * 0x1C);
    uint32_t nodeHead = 7;
    uint32_t freeCount = 7;
    linkNodes(nodeHead, freeCount, nodes.address(), 64);
    bool ring = nodeHead == nodes.address() && freeCount == 64;
    for (uint32_t i = 0; i < 64; ++i) {
        const uintptr_t node = nodes.address() + i * 0x1C;
        const uintptr_t next = nodes.address() + ((i + 1) % 64) * 0x1C;
        const uintptr_t previous = nodes.address() + ((i + 63) % 64) * 0x1C;
        ring = ring && load(node) == next && load(node + 4) == previous;
    }
    check(ring, "64 category nodes: a ring in order, the head the first node, the free count 64 (0x9291C0)");
}

void theCustomisationRecords() {
    using namespace vehicle_pool_init;
    Block object(0x54);
    resetMempool(object.address(), "vcust cb %d", 5, 0x40001000, kCbBufferBytes);
    check(std::string(reinterpret_cast<const char*>(object.address() + 0xC)) == "vcust cb 5" && load(object.address() + 0x38) == 0x40001000 &&
              load(object.address() + 0x3C) == 0x1800 && load(object.address() + 0x40) == 1 && load(object.address() + 0x4C) == 0x1800 &&
              load(object.address() + 0x50) == 0x1800,
          "a customisation mempool object: its name, buffer, size, flag, limit and capacity");
    Block ext(0x54);
    resetMempool(ext.address(), "ext vcust cb %d", 63, 0x50000000, kExtBufferBytes);
    check(std::string(reinterpret_cast<const char*>(ext.address() + 0xC)) == "ext vcust cb 63" && load(ext.address() + 0x3C) == 0x1400,
          "and the extended one with its smaller buffer");
    Block instance(0x84);
    markCustomInstance(instance.address());
    check(load(instance.address() + 0x30) == kMinusOne && load(instance.address() + 0x40) == kMinusOne && load(instance.address() + 0x50) == kMinusOne &&
              load(instance.address() + 0x5C) == kMinusOne && load(instance.address() + 0x34) == 0 && load(instance.address() + 0x7C) == 0,
          "a customisation instance element: four index words are -1, the rest zero");
    bool sizes = true;
    for (const auto& allocator : vehicle_pool_sites::kCustomAllocators) {
        sizes = sizes && allocator.grownCount == (allocator.shippedCount / 32) * 64;
    }
    check(sizes && vehicle_pool_sites::kCustomAllocators[0].grownCount == 0x40 && vehicle_pool_sites::kCustomAllocators[1].grownCount == 0xC80 &&
              vehicle_pool_sites::kCustomAllocators[2].grownCount == 0x1900,
          "the customisation allocators double to 0x40, 0xC80 and 0x1900");
}

}  // namespace

int main() {
    theReferenceTableMatchesTheInventory();
    theAddressMath();
    theBoundsAndTheHeadroomBlocks();
    theClearance();
    theConstructors();
    theByteTableReset();
    theFreeLists();
    theCustomisationRecords();
    return g_failures == 0 ? 0 : 1;
}
