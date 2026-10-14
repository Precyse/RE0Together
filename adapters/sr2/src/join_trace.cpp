#include "join_trace.h"

#include <atomic>
#include <cstdint>

#include "coop_limits.h"
#include "hooks.h"
#include "log.h"
#include "member_slots.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

using PoolSetupFn = uint32_t(__cdecl*)();
using GetterFn = uintptr_t(__cdecl*)();
using JoinQueueAddFn = void(__stdcall*)(uintptr_t session, uintptr_t request);

PoolSetupFn g_poolSetup = nullptr;
GetterFn g_slotAllocator = nullptr;
JoinQueueAddFn g_joinQueueAdd = nullptr;

std::atomic<uintptr_t> g_lastSlot{0};
uint32_t g_maxPlayers = sr2::kCoopDefaultPlayers;

uint32_t __cdecl detourPoolSetup() {
    logger::write("trace: session pools sized, max session players %u, max players %u, connections per player %u, inside net_set_mode %d",
                  readDword(sr2::kMaxSessionPlayers), readDword(sr2::kMaxPlayers), readDword(sr2::kConnectionsPerPlayer),
                  coop_limits::insideNetSetMode() ? 1 : 0);
    logger::write("trace: coop_allocators used %u of %u bytes, player N=8 allocator count %u block 0x%X",
                  readDword(sr2::kCoopAllocatorsPool + sr2::kPoolUsedOffset),
                  readDword(sr2::kCoopAllocatorsPool + sr2::kPoolSizeOffset),
                  readDword(sr2::kPlayerN8Allocator + sr2::kAllocatorCountOffset),
                  readDword(sr2::kPlayerN8Allocator + sr2::kAllocatorBlockOffset));
    member_slots::check(g_maxPlayers);
    return g_poolSetup();
}

uintptr_t __cdecl detourSlotAllocator() {
    const uintptr_t result = g_slotAllocator();
    if (g_lastSlot.exchange(result) != result) {
        logger::write("trace: player slot allocator returned 0x%X", static_cast<unsigned>(result));
        member_slots::check(g_maxPlayers);
    }
    return result;
}

void __stdcall detourJoinQueueAdd(uintptr_t session, uintptr_t request) {
    const uintptr_t countAddress = session + sr2::kSessionJoinQueueCountOffset;
    const uint32_t before = readDword(countAddress);
    g_joinQueueAdd(session, request);
    logger::write("trace: join request queued, session 0x%X, request 0x%X, queue %u -> %u",
                  static_cast<unsigned>(session), static_cast<unsigned>(request), before, readDword(countAddress));
    member_slots::check(g_maxPlayers);
}

}  // namespace

namespace join_trace {

void install(uint32_t maxPlayers) {
    g_maxPlayers = maxPlayers;
    hooks::install("trace_session_pool_setup", sr2::kSessionPoolSetup, reinterpret_cast<void*>(&detourPoolSetup),
                   reinterpret_cast<void**>(&g_poolSetup));
    hooks::install("trace_player_slot_allocator", sr2::kPlayerSlotAllocator,
                   reinterpret_cast<void*>(&detourSlotAllocator), reinterpret_cast<void**>(&g_slotAllocator));
    hooks::install("trace_join_queue_add", sr2::kJoinQueueAdd, reinterpret_cast<void*>(&detourJoinQueueAdd),
                   reinterpret_cast<void**>(&g_joinQueueAdd));
}

}  // namespace join_trace
