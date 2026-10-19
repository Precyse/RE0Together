#include "spawn_budget.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "guest_call.h"
#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "population.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "spawn_budget_rules.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "vehicle_trace.h"

namespace {

// The group create's 7 stack arguments (its callers pop them); the first is the group set it builds.
using GroupCreateFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

constexpr uint64_t kCensusIntervalMs = 60000;
constexpr size_t kChunkNodeFirstArg = 0;
constexpr size_t kChunkNodeSecondArg = 1;

bool g_trace = false;
GroupCreateFn g_groupCreate = nullptr;
void* g_npcSetupOriginal = nullptr;
void* g_chunkNodeOriginal = nullptr;
std::atomic<uint64_t> g_backoffUntilMs{0};  // the ambient caller is refused until then
rate_limit::RateLimit g_reserveLimit;       // the refusal lines, one limit per kind
rate_limit::RateLimit g_noRoomLimit;
rate_limit::RateLimit g_reservedLimit;
rate_limit::RateLimit g_partialLimit;

// The census (trace=1): the group create counted per caller and outcome, the NPC human setup and the chunk node loader per
// caller, one line a minute.
std::mutex g_censusMutex;
rate_limit::RateLimit g_censusLimit(kCensusIntervalMs);
spawn_budget_rules::GroupCreateCounts g_groupCalls;
spawn_budget_rules::CallerCounts<4> g_npcSetups(
    {sr2::kNpcCreateReturnA, sr2::kNpcCreateReturnB, sr2::kNpcCreateReturnC, sr2::kNpcCreateReturnD});
spawn_budget_rules::TakenByCaller<4> g_chunkNodes(
    {sr2::kChunkNodeReturnA, sr2::kChunkNodeReturnB, sr2::kChunkNodeReturnC, sr2::kChunkNodeReturnD});

uint64_t millisNow() { return GetTickCount64(); }

// The entries of a group set: count at +0, array at +4.
std::vector<spawn_budget_rules::GroupEntry> groupEntries(uintptr_t groupSet) {
    std::vector<spawn_budget_rules::GroupEntry> entries;
    const uint32_t count = readDword(groupSet + sr2::kGroupSetCountOffset);
    const uintptr_t array = readDword(groupSet + sr2::kGroupSetEntriesOffset);
    for (uint32_t i = 0; i < count && i < sr2::kMaxSpawnGroups; ++i) {
        const uintptr_t entry = array + i * sr2::kGroupSetEntryStride;
        entries.push_back({readDword(entry + sr2::kEntryElementsOffset), readDword(entry + sr2::kEntryExtraOffset) != 0});
    }
    return entries;
}

// The free nodes of a circular free list whose head is at `headGlobal`, counted up to `limit`, linked at `linkOffset`.
uint32_t freeNodes(uintptr_t headGlobal, uint32_t limit, uintptr_t linkOffset = sr2::kPoolLinkOffset) {
    return spawn_budget_rules::countCircular(readDword(headGlobal), limit, [&](uintptr_t node) { return readDword(node + linkOffset); });
}

// The active groups (circular), each with the element slots its template says it has.
std::vector<spawn_budget_rules::Group> activeGroups() {
    std::vector<spawn_budget_rules::Group> groups;
    const uintptr_t head = readDword(sr2::kGroupListHead);
    uintptr_t group = head;
    for (size_t i = 0; group != 0 && i < sr2::kMaxSpawnGroups; ++i) {
        spawn_budget_rules::Group entry{group, {}};
        const uintptr_t groupTemplate = readDword(group + sr2::kGroupTemplateOffset);
        uint8_t slotCount = 0;
        if (groupTemplate) readMemory(groupTemplate + sr2::kTemplateSlotCountOffset, &slotCount, sizeof(slotCount));
        for (uint8_t slot = 0; slot < slotCount; ++slot) {
            entry.slots.push_back(readDword(group + sr2::kGroupSlotsOffset + slot * sizeof(uint32_t)));
        }
        groups.push_back(std::move(entry));
        group = readDword(group + sr2::kPoolLinkOffset);
        if (group == head) break;
    }
    return groups;
}

// The refusal lines, at most one a second per kind; "K since the last line" counts the refusals of that kind. A reserved
// caller's refusal is the signal that the reserve is too small.
void traceRefused(uintptr_t caller, spawn_budget_rules::Verdict verdict, const spawn_budget_rules::Need& need, uint32_t freeGroups,
                  uint32_t freeElements) {
    using spawn_budget_rules::Verdict;
    const bool ambient = spawn_budget_rules::isAmbientCaller(caller);
    rate_limit::RateLimit& limit = !ambient ? g_reservedLimit : verdict == Verdict::Reserve ? g_reserveLimit : g_noRoomLimit;
    uint32_t refused = 0;
    if (!limit.allow(millisNow(), refused)) return;
    if (!ambient) {
        logger::write("trace: spawn budget: reserved create from 0x%X refused, needs %u groups and %u elements, %u and %u free (%u since the last line)",
                      static_cast<unsigned>(caller), need.groups, need.elements, freeGroups, freeElements, refused);
    } else if (verdict == Verdict::Reserve) {
        logger::write("trace: spawn budget: ambient create from 0x%X refused by the reserve, needs %u groups and %u elements, %u and %u free, "
                      "reserve %u and %u (%u since the last line)",
                      static_cast<unsigned>(caller), need.groups, need.elements, freeGroups, freeElements,
                      spawn_budget_rules::kAmbientReserveGroups, spawn_budget_rules::kAmbientReserveElements, refused);
    } else {
        logger::write("trace: spawn budget: ambient create from 0x%X refused, no room, needs %u groups and %u elements, %u and %u free (%u since the last line)",
                      static_cast<unsigned>(caller), need.groups, need.elements, freeGroups, freeElements, refused);
    }
}

void tracePartialGroups() {
    const std::vector<uintptr_t> partial = spawn_budget_rules::partialGroups(activeGroups());
    uint32_t lines = 0;
    if (partial.empty() || !g_partialLimit.allow(millisNow(), lines)) return;
    logger::write("trace: spawn budget: %zu partial group(s) after a group create, first 0x%X (%u lines since the last)", partial.size(),
                  static_cast<unsigned>(partial.front()), lines);
}

void noteGroupCreate(uintptr_t caller, spawn_budget_rules::Outcome outcome) {
    std::lock_guard<std::mutex> lock(g_censusMutex);
    g_groupCalls.record(spawn_budget_rules::callerOf(caller), outcome);
}

// The census line, at most once a minute: the free nodes of the static pools beside the game's own in-use counts, the group
// creates per caller and outcome, what the chunk node loader took, the vehicle pool and the remote vehicle creates, the NPC
// setups, the remote players and the player areas of the last population budget.
void traceCensus() {
    uint32_t triggers = 0;
    if (!g_censusLimit.allow(millisNow(), triggers)) return;
    spawn_budget_rules::Census census{freeNodes(sr2::kGroupFreeHead, sr2::kMaxSpawnGroups),
                                      freeNodes(sr2::kElementFreeHead, sr2::kSpawnElementPool),
                                      readDword(sr2::kGroupsInUse),
                                      readDword(sr2::kElementsInUse),
                                      freeNodes(sr2::kNpcHumanFreeHead, sr2::kNpcHumanPool, sr2::kNpcHumanLinkOffset),
                                      {},
                                      {},
                                      vehicle_trace::freeVehicles(),
                                      vehicle_trace::freeCreationData(),
                                      vehicle_trace::totals(),
                                      {},
                                      remote_players::remoteHumanCount(),
                                      remote_players::remoteMemberCount(),
                                      population::lastAreasHundredths()};
    {
        std::lock_guard<std::mutex> lock(g_censusMutex);
        census.groupCreates = g_groupCalls.describe();
        census.chunkNodes = g_chunkNodes.describe();
        census.npcCreates = g_npcSetups.describe();
    }
    logger::write("%s", spawn_budget_rules::censusLine(census).c_str());
}

// The verdict for one group create (docs/SR2_NOTES.md, "Spawn budget"): every caller needs the room its group set takes, the
// ambient caller also keeps the reserve free and waits after a refusal (no walk then). Carries what the refusal line reports.
struct Gate {
    spawn_budget_rules::Verdict verdict;
    spawn_budget_rules::Need need;
    uint32_t freeGroups;
    uint32_t freeElements;
};

Gate gateFor(uintptr_t caller, uint32_t groupSet) {
    using namespace spawn_budget_rules;
    const bool ambient = isAmbientCaller(caller);
    const bool waiting = ambient && inBackoff(millisNow(), g_backoffUntilMs);
    Gate gate{Verdict::Backoff, {}, 0, 0};
    if (!waiting) {
        gate.need = needFor(groupEntries(groupSet));
        gate.freeGroups = freeNodes(sr2::kGroupFreeHead, walkLimit(ambient, gate.need.groups, kAmbientReserveGroups));
        gate.freeElements = freeNodes(sr2::kElementFreeHead, walkLimit(ambient, gate.need.elements, kAmbientReserveElements));
    }
    gate.verdict = decide(ambient, gate.need, gate.freeGroups, gate.freeElements, waiting);
    if (startsBackoff(ambient, gate.verdict)) g_backoffUntilMs = millisNow() + kAmbientRetryMs;
    return gate;
}

uint32_t __cdecl budgetedGroupCreate(uint32_t groupSet, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    using spawn_budget_rules::Verdict;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Verdict verdict = Verdict::Create;
    if (groupSet != 0) {
        const Gate gate = gateFor(caller, groupSet);
        verdict = gate.verdict;
        if (verdict != Verdict::Create) {
            if (g_trace) {
                if (verdict != Verdict::Backoff) traceRefused(caller, verdict, gate.need, gate.freeGroups, gate.freeElements);
                noteGroupCreate(caller, spawn_budget_rules::outcomeOf(verdict, false));
            }
            return 0;
        }
    }
    const uint32_t group = g_groupCreate(groupSet, a2, a3, a4, a5, a6, a7);
    if (g_trace) {
        noteGroupCreate(caller, spawn_budget_rules::outcomeOf(verdict, group != 0));
        tracePartialGroups();
        traceCensus();
    }
    return group;
}

// NC1: the NPC human setup 0x9D0DE0 gets the human a create returned in esi; 0 is a create that failed (the pool is dry).
void __cdecl traceNpcSetup(GuestFrame* frame) {
    std::lock_guard<std::mutex> lock(g_censusMutex);
    g_npcSetups.record(frame->returnAddress, frame->regs.esi == 0);
}

TRACE_ENTRY_STUB(npcSetupStub, traceNpcSetup, g_npcSetupOriginal)

uint32_t positiveDifference(uint32_t before, uint32_t after) { return after > before ? after - before : 0; }

// NC2: the chunk node loader 0x9140C0, cdecl(int, int), returns al and takes no register. It takes groups and elements
// without going through the group create; the game's in-use counters before and after say how many, per caller.
void __cdecl traceChunkNodes(GuestFrame* frame) {
    const uint32_t groupsBefore = readDword(sr2::kGroupsInUse);
    const uint32_t elementsBefore = readDword(sr2::kElementsInUse);
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_chunkNodeOriginal), frame->regs,
                                       {frame->args[kChunkNodeFirstArg], frame->args[kChunkNodeSecondArg]});
    const uint32_t groups = positiveDifference(groupsBefore, readDword(sr2::kGroupsInUse));
    const uint32_t elements = positiveDifference(elementsBefore, readDword(sr2::kElementsInUse));
    std::lock_guard<std::mutex> lock(g_censusMutex);
    g_chunkNodes.add(frame->returnAddress, groups, elements);
}

PARTNER_WRAP_STUB(chunkNodeStub, traceChunkNodes, 0)

}  // namespace

namespace spawn_budget {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::TraceNpcSetup: return {reinterpret_cast<void*>(&npcSetupStub), &g_npcSetupOriginal};
        case partner_sites::Thunk::TraceChunkNodes: return {reinterpret_cast<void*>(&chunkNodeStub), &g_chunkNodeOriginal};
        default: return {nullptr, nullptr};
    }
}

bool installHook() {
    return hooks::install("spawn_budget_group_create", sr2::kGroupCreate, reinterpret_cast<void*>(&budgetedGroupCreate),
                          reinterpret_cast<void**>(&g_groupCreate));
}

void removeHook() { hooks::remove(sr2::kGroupCreate); }

}  // namespace spawn_budget
