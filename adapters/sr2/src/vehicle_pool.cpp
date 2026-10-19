#include "vehicle_pool.h"

#include <windows.h>

#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "code_patch.h"
#include "log.h"
#include "memory.h"
#include "partner_replacement.h"
#include "population_rules.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "vehicle_pool_init.h"
#include "vehicle_pool_references.h"
#include "vehicle_pool_rules.h"
#include "vehicle_pool_sites.h"

namespace {

using vehicle_pool_sites::kGrownEntries;
using vehicle_pool_sites::kShippedEntries;
using vehicle_pool_sites::kTableCount;
using vehicle_pool_sites::Table;
using vehicle_pool_sites::TableId;

constexpr size_t kAllocatorCount = std::size(vehicle_pool_sites::kCustomAllocators);
constexpr uint32_t kNodesAllFree = kShippedEntries;

bool g_trace = false;
bool g_wanted = false;
bool g_tried = false;
bool g_installed = false;
uint32_t g_newBase[kTableCount] = {};  // where each table is now (its shipped address until the move)
rate_limit::RateLimit g_guardLimit[kTableCount];

size_t indexOf(TableId id) { return static_cast<size_t>(id); }

std::vector<uint8_t> littleEndian(uint32_t value) {
    return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
}

// ---- The game's constructors the new tables need ----

void constructVehicle(uintptr_t object) { reinterpret_cast<void*(__thiscall*)(void*)>(sr2::kVehicleConstructor)(reinterpret_cast<void*>(object)); }

void constructMempool(uintptr_t object) { reinterpret_cast<void*(__thiscall*)(void*)>(sr2::kMempoolConstructor)(reinterpret_cast<void*>(object)); }

void constructSixteenByteVector(uintptr_t first) {
    reinterpret_cast<void(__stdcall*)(void*, uint32_t, uint32_t, void*)>(sr2::kVectorConstructor)(
        reinterpret_cast<void*>(first), sr2::kT1VectorElementSize, sr2::kT1VectorElementCount,
        reinterpret_cast<void*>(sr2::kT1VectorElementConstructor));
}

const vehicle_pool_init::Constructors kGameConstructors{&constructVehicle, &constructMempool, &constructSixteenByteVector};

// ---- The headroom call blocks (HD1, HD2) ----

// What the two parked vehicle spawners test: the vehicles still allowed in the pool, as population_rules::poolHeadroom.
int32_t __cdecl poolHeadroomNow() {
    return population_rules::poolHeadroom(static_cast<int32_t>(vehicle_pool::capacity()), static_cast<int32_t>(readDword(sr2::kVehicleCount)),
                                          remote_players::remoteMemberCount());
}

// HD1 loads the headroom into ecx and HD2 into eax; every other register survives (the flags are set by the compare after).
void __declspec(naked) headroomEcxStub() {
    __asm {
        push eax
        push edx
        call poolHeadroomNow
        mov ecx, eax
        pop edx
        pop eax
        ret
    }
}

void __declspec(naked) headroomEaxStub() {
    __asm {
        push ecx
        push edx
        call poolHeadroomNow
        pop edx
        pop ecx
        ret
    }
}

uintptr_t stubOf(partner_sites::Thunk thunk) {
    const partner_thunks::Binding binding = vehicle_pool::bind(thunk);
    return reinterpret_cast<uintptr_t>(binding.entry);
}

// ---- What the move writes ----

// The checks (no replacement) or the writes of the whole move: every reference dword, every bound immediate, both headroom
// blocks. `newBase` is where each table lives after the move; null for the checks.
std::vector<CodePatch> movePatches(const uint32_t* newBase) {
    std::vector<CodePatch> patches;
    for (const vehicle_pool_references::Reference& reference : vehicle_pool_references::kReferences) {
        const Table& table = vehicle_pool_sites::tableOf(reference.table);
        CodePatch patch{table.name, reference.site, littleEndian(reference.value), {}};
        if (newBase) patch.replacement = littleEndian(vehicle_pool_rules::relocated(table, newBase[indexOf(reference.table)], reference.value));
        patches.push_back(std::move(patch));
    }
    for (const vehicle_pool_sites::Bound& bound : vehicle_pool_sites::kBounds) {
        patches.push_back({vehicle_pool_sites::tableOf(bound.table).name, bound.address, partner_sites::parseHex(bound.expected),
                           newBase ? partner_sites::parseHex(bound.replacement) : std::vector<uint8_t>{}});
    }
    for (const partner_sites::Site& site : vehicle_pool_sites::kHeadroomBlocks) {
        patches.push_back({site.id, site.address, partner_sites::parseHex(site.expected),
                           newBase ? partner_replacement::replacementFor(site, stubOf(site.thunk)) : std::vector<uint8_t>{}});
    }
    return patches;
}

// ---- The state the move needs ----

// Empty: nothing of the world exists yet. Returns the reason the move cannot be made, empty when it can.
std::string stateProblem() {
    using vehicle_pool_rules::shippedCountByteAt;
    if (readDword(sr2::kVehicleCount) != 0) return "vehicles are in use";
    if (readDword(sr2::kVehicleFreeHead) != 0) return "the world is already initialised";
    for (const Table& table : vehicle_pool_sites::kTables) {
        if (table.countAndStack && readByte(shippedCountByteAt(table)) != 0) return std::string("table ") + table.name + " is in use";
    }
    const uint32_t freeNodes = readDword(sr2::kCategoryFreeNodes);
    if (freeNodes != 0 && freeNodes != kNodesAllFree) return "vehicle category nodes are in use";
    const Table& pointers = vehicle_pool_sites::tableOf(TableId::SlotPointers);
    for (uint32_t i = 0; i < kShippedEntries; ++i) {
        if (readDword(pointers.base + i * pointers.elementSize) != 0) return "a customisation slot is in use";
    }
    for (const vehicle_pool_sites::CustomAllocator& allocator : vehicle_pool_sites::kCustomAllocators) {
        const bool hasBlock = readDword(allocator.address + sr2::kAllocatorBlockOffset) != 0;
        const bool active = readByte(allocator.address + sr2::kAllocatorActiveOffset) != 0;
        if (readDword(allocator.address + sr2::kAllocatorCountOffset) != allocator.shippedCount) {
            return std::string(allocator.name) + " allocator does not hold the shipped count";
        }
        if (hasBlock != active) return std::string(allocator.name) + " allocator is half activated";
    }
    if (readDword(sr2::kVehicleDeltacompCount) != sr2::kVehicleDeltacompShipped) return "the vehicle deltacomp allocator does not hold 480";
    if (readDword(sr2::kCategoryClass1Limit) != sr2::kCategoryClass1ShippedLimit) return "the class 1 category limit is not 12";
    return {};
}

// ---- The blocks ----

struct Blocks {
    void* tables[kTableCount] = {};
    void* extra[kAllocatorCount] = {};  // the free elements an active customisation allocator gets
    bool ok = true;

    void* take(uint32_t bytes) {
        void* block = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!block) ok = false;
        return block;
    }

    void release() {
        for (void* block : tables) {
            if (block) VirtualFree(block, 0, MEM_RELEASE);
        }
        for (void* block : extra) {
            if (block) VirtualFree(block, 0, MEM_RELEASE);
        }
    }
};

uint32_t extraBytes(const vehicle_pool_sites::CustomAllocator& allocator) {
    return (allocator.grownCount - allocator.shippedCount) * allocator.elementSize;
}

bool allocatorIsActive(const vehicle_pool_sites::CustomAllocator& allocator) {
    return readDword(allocator.address + sr2::kAllocatorBlockOffset) != 0;
}

Blocks allocateBlocks() {
    Blocks blocks;
    for (size_t i = 0; i < kTableCount; ++i) blocks.tables[i] = blocks.take(vehicle_pool_rules::blockBytes(vehicle_pool_sites::kTables[i]));
    for (size_t i = 0; i < kAllocatorCount; ++i) {
        const vehicle_pool_sites::CustomAllocator& allocator = vehicle_pool_sites::kCustomAllocators[i];
        if (allocatorIsActive(allocator)) blocks.extra[i] = blocks.take(extraBytes(allocator));
    }
    return blocks;
}

// The new tables in the state the game's start-up leaves the shipped ones in (docs/SR2_NOTES.md): constructed entries, the
// byte tables' stacks, the customisation mempool objects and their buffers.
void prepareTables(const uint32_t* base) {
    using namespace vehicle_pool_init;
    for (const Table& table : vehicle_pool_sites::kTables) {
        const uintptr_t block = base[indexOf(table.id)];
        construct(table.id, block, kGameConstructors);
        if (table.countAndStack) resetByteTable(vehicle_pool_rules::countByteAt(table, static_cast<uint32_t>(block)), kGrownEntries);
    }
    const uint32_t buffersCb = base[indexOf(TableId::BuffersCb)];
    const uint32_t buffersExt = base[indexOf(TableId::BuffersExt)];
    const Table& objects = vehicle_pool_sites::tableOf(TableId::ObjectsCb);
    for (uint32_t i = 0; i < kGrownEntries; ++i) {
        const uintptr_t slot = i * objects.elementSize;
        resetMempool(base[indexOf(TableId::ObjectsCb)] + slot, "vcust cb %d", i, buffersCb + i * kCbBufferBytes, kCbBufferBytes);
        resetMempool(base[indexOf(TableId::ObjectsExt)] + slot, "ext vcust cb %d", i, buffersExt + i * kExtBufferBytes, kExtBufferBytes);
    }
}

// ---- After the code is written ----

// The category nodes of the vehicles, when the game already linked the 32 shipped ones: link the 64 new ones the same way. (Not
// yet linked, the world's own start-up does it with the new bound.)
void relinkNodes(uint32_t firstNode) {
    if (readDword(sr2::kCategoryFreeNodes) != kNodesAllFree) return;
    vehicle_pool_init::linkNodes(dwordField(sr2::kCategoryFreeHead, 0), dwordField(sr2::kCategoryFreeNodes, 0), firstNode, kGrownEntries);
}

// The customisation allocators hold 64 vehicles' worth. Inactive: the count is all there is to do (the activation builds the
// elements). Active: the missing elements go onto the free list under the allocator's lock, from a block of ours.
void growAllocator(const vehicle_pool_sites::CustomAllocator& allocator, void* extra) {
    const uintptr_t object = allocator.address;
    dwordField(object, sr2::kAllocatorCountOffset) = allocator.grownCount;
    if (!extra) return;
    CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(object + sr2::kAllocatorLockOffset);
    EnterCriticalSection(lock);
    uint32_t& head = dwordField(object, sr2::kAllocatorFreeHeadOffset);
    const uint32_t added = allocator.grownCount - allocator.shippedCount;
    for (uint32_t i = 0; i < added; ++i) {
        const uintptr_t element = reinterpret_cast<uintptr_t>(extra) + i * allocator.elementSize;
        if (allocator.elementSize == vehicle_pool_init::kCustomInstanceSize) vehicle_pool_init::markCustomInstance(element);
        vehicle_pool_init::pushFree(head, element, allocator.nextOffset, allocator.previousOffset);
    }
    LeaveCriticalSection(lock);
}

}  // namespace

namespace vehicle_pool {

bool request() {
    for (size_t i = 0; i < kTableCount; ++i) g_newBase[i] = vehicle_pool_sites::kTables[i].base;
    const bool match = codeMatches(movePatches(nullptr));
    g_wanted = match;
    if (match) {
        logger::write("vehicle pool: %zu references, %zu bounds and %zu headroom blocks match", std::size(vehicle_pool_references::kReferences),
                      std::size(vehicle_pool_sites::kBounds), std::size(vehicle_pool_sites::kHeadroomBlocks));
    } else {
        logger::write("vehicle pool: not installed, the exe differs from the research");
    }
    return match;
}

bool wanted() { return g_wanted; }

bool installed() { return g_installed; }

uint32_t capacity() { return g_installed ? kGrownEntries : kShippedEntries; }

uintptr_t countByteAddress(TableId table) {
    const Table& layout = vehicle_pool_sites::tableOf(table);
    return g_installed ? vehicle_pool_rules::countByteAt(layout, g_newBase[indexOf(table)]) : vehicle_pool_rules::shippedCountByteAt(layout);
}

uintptr_t slotPointersAddress() { return g_newBase[indexOf(TableId::SlotPointers)]; }

bool install() {
    if (!g_wanted || g_tried) return g_installed;
    g_tried = true;
    const std::string problem = stateProblem();
    if (!problem.empty()) {
        logger::write("vehicle pool: not installed, %s", problem.c_str());
        return false;
    }
    Blocks blocks = allocateBlocks();
    if (!blocks.ok) {
        blocks.release();
        logger::write("vehicle pool: not installed, the memory for 64 vehicles is not available");
        return false;
    }
    uint32_t base[kTableCount];
    for (size_t i = 0; i < kTableCount; ++i) base[i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(blocks.tables[i]));
    prepareTables(base);
    if (!applyCodePatches(movePatches(base))) {
        blocks.release();
        logger::write("vehicle pool: not installed, the code edits were not applied");
        return false;
    }
    memcpy(g_newBase, base, sizeof(g_newBase));
    relinkNodes(base[indexOf(TableId::Nodes)]);
    for (size_t i = 0; i < kAllocatorCount; ++i) growAllocator(vehicle_pool_sites::kCustomAllocators[i], blocks.extra[i]);
    g_installed = true;
    logger::write("vehicle pool: %u vehicles, %zu references and %zu bounds rewritten", kGrownEntries, std::size(vehicle_pool_references::kReferences),
                  std::size(vehicle_pool_sites::kBounds));
    return true;
}

void onCoopActivation(uint32_t maxPlayers) {
    if (!g_installed) return;
    dwordField(sr2::kCategoryClass1Limit, 0) = sr2::kCategoryClass1PerPlayer * maxPlayers;
    dwordField(sr2::kVehicleCreationCount, 0) = sr2::kVehicleCreationGrown;
    logger::write("vehicle pool: class 1 limit %u, vehicle creation records %u", sr2::kCategoryClass1PerPlayer * maxPlayers, sr2::kVehicleCreationGrown);
}

void onModeSet(int mode) {
    if (!g_installed || mode == sr2::kNetModeCoop) return;
    dwordField(sr2::kCategoryClass1Limit, 0) = sr2::kCategoryClass1ShippedLimit;
    dwordField(sr2::kVehicleCreationCount, 0) = sr2::kVehicleCreationShipped;
}

void setTrace(bool trace) { g_trace = trace; }

void traceGuard() {
    if (!g_trace || !g_installed) return;
    const uint32_t inUse = readDword(sr2::kVehicleCount);
    if (inUse >= kGrownEntries) return;
    for (const Table& table : vehicle_pool_sites::kTables) {
        if (!table.countAndStack || readByte(countByteAddress(table.id)) < kGrownEntries) continue;
        uint32_t suppressed = 0;
        if (g_guardLimit[indexOf(table.id)].allow(GetTickCount64(), suppressed)) {
            logger::write("trace: vehicles: side table %s empty with %u vehicles in use", table.name, inUse);
        }
    }
}

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::VehicleHeadroomEcx: return {reinterpret_cast<void*>(&headroomEcxStub), nullptr};
        case partner_sites::Thunk::VehicleHeadroomEax: return {reinterpret_cast<void*>(&headroomEaxStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace vehicle_pool
