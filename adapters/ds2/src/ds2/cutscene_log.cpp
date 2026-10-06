// DEATH STRANDING 2: cutscene log (adapter.ini cutscene_log=1, off by default): every shared Sequence start (the hook is
// ds2/cutscene.cpp) with its resource, entity and network UUIDs, category and stop frame, and every change of the
// DSGameState bits (the cutscene states), for the live checks of analysis/STORY_SYNC_PLAN.md.
#include "ds2/cutscene_log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "log.h"

namespace {

constexpr uintptr_t kGameStateGlobal = 0x14623E4C0;
constexpr uintptr_t kBitsLow = 0x160, kBitsHigh = 0x168;
constexpr ULONGLONG kBitsPollMs = 250;
constexpr uintptr_t kFrame = 0x34C;

std::atomic<bool> g_enabled{false};

void tick() {
    static ULONGLONG last = 0;
    static uint64_t lastLow = ~0ull, lastHigh = ~0ull;
    const ULONGLONG now = GetTickCount64();
    if (now - last < kBitsPollMs) return;
    last = now;
    const uintptr_t state = decima::readPointer(ds2::at(kGameStateGlobal));
    if (!state) return;
    const uint64_t low = ds2::field<uint64_t>(state, kBitsLow), high = ds2::field<uint64_t>(state, kBitsHigh);
    if (low != lastLow || high != lastHigh) {
        lastLow = low;
        lastHigh = high;
        logger::write("cutscene_log: game state bits low %016llx high %016llx", static_cast<unsigned long long>(low),
                      static_cast<unsigned long long>(high));
    }
}

void hex(const uint8_t* bytes, char* out) {
    for (size_t i = 0; i < sequence_info::kUuidSize; ++i) std::snprintf(out + i * 2, 3, "%02x", bytes[i]);
}

}  // namespace

namespace cutscene_log {

void enable() {
    g_enabled = true;
    sim_tick::add(&tick, "cutscene log");
}

void onStart(uintptr_t sequence, const sequence_info::Info& info, const char* decision) {
    if (!g_enabled) return;
    char resource[sequence_info::kUuidSize * 2 + 1], entity[sizeof(resource)], network[sizeof(resource)];
    hex(info.resource, resource);
    hex(info.entity, entity);
    hex(info.network, network);
    logger::writeUnlessRepeated("cutscene_log: Sequence start %p %s frame %d stop %d category %u state %u resource %s entity %s network %s",
                  reinterpret_cast<void*>(sequence), decision, sequence_info::frame(sequence), info.stopFrame, info.category, info.gameState,
                  resource, entity, network);
}

void onUnread(uintptr_t sequence) {
    if (!g_enabled) return;
    const sequence_info::Probe p = sequence_info::probe(sequence);
    logger::write("cutscene_log: Sequence start %p unread: ref %p flags %016llx holder %p resource %p vtable %p", reinterpret_cast<void*>(sequence),
                  reinterpret_cast<void*>(p.ref), static_cast<unsigned long long>(p.flags), reinterpret_cast<void*>(p.holder),
                  reinterpret_cast<void*>(p.resource), reinterpret_cast<void*>(p.resourceVtable));
}

}  // namespace cutscene_log
