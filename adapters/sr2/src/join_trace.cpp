#include "join_trace.h"

#include <atomic>
#include <cstdint>

#include "coop_limits.h"
#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

using PoolSetupFn = uint32_t(__cdecl*)();
using GetterFn = uintptr_t(__cdecl*)();
using JoinQueueAddFn = void(__stdcall*)(uintptr_t session, uintptr_t request);

PoolSetupFn g_poolSetup = nullptr;
GetterFn g_remotePlayer = nullptr;
GetterFn g_slotAllocator = nullptr;
JoinQueueAddFn g_joinQueueAdd = nullptr;

std::atomic<uintptr_t> g_lastRemotePlayer{0};
std::atomic<uintptr_t> g_lastSlot{0};

uint32_t readU32(uintptr_t address) {
    uint32_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

uint32_t __cdecl detourPoolSetup() {
    logger::write("trace: session pools sized, max session players %u, max players %u, connections per player %u, inside net_set_mode %d",
                  readU32(sr2::kMaxSessionPlayers), readU32(sr2::kMaxPlayers), readU32(sr2::kConnectionsPerPlayer),
                  coop_limits::insideNetSetMode() ? 1 : 0);
    return g_poolSetup();
}

uintptr_t __cdecl detourRemotePlayer() {
    const uintptr_t result = g_remotePlayer();
    if (g_lastRemotePlayer.exchange(result) != result) {
        logger::write("trace: remote player getter now 0x%X", static_cast<unsigned>(result));
    }
    return result;
}

uintptr_t __cdecl detourSlotAllocator() {
    const uintptr_t result = g_slotAllocator();
    if (g_lastSlot.exchange(result) != result) {
        logger::write("trace: player slot allocator returned 0x%X", static_cast<unsigned>(result));
    }
    return result;
}

void __stdcall detourJoinQueueAdd(uintptr_t session, uintptr_t request) {
    const uintptr_t countAddress = session + sr2::kSessionJoinQueueCountOffset;
    const uint32_t before = readU32(countAddress);
    g_joinQueueAdd(session, request);
    logger::write("trace: join request queued, session 0x%X, request 0x%X, queue %u -> %u",
                  static_cast<unsigned>(session), static_cast<unsigned>(request), before, readU32(countAddress));
}

}  // namespace

namespace join_trace {

void install() {
    hooks::install("trace_session_pool_setup", sr2::kSessionPoolSetup, reinterpret_cast<void*>(&detourPoolSetup),
                   reinterpret_cast<void**>(&g_poolSetup));
    hooks::install("trace_remote_player_getter", sr2::kRemotePlayerGetter,
                   reinterpret_cast<void*>(&detourRemotePlayer), reinterpret_cast<void**>(&g_remotePlayer));
    hooks::install("trace_player_slot_allocator", sr2::kPlayerSlotAllocator,
                   reinterpret_cast<void*>(&detourSlotAllocator), reinterpret_cast<void**>(&g_slotAllocator));
    hooks::install("trace_join_queue_add", sr2::kJoinQueueAdd, reinterpret_cast<void*>(&detourJoinQueueAdd),
                   reinterpret_cast<void**>(&g_joinQueueAdd));
}

}  // namespace join_trace
