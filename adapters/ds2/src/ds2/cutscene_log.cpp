// DEATH STRANDING 2: cutscene log (adapter.ini cutscene_log=1, off by default): every shared Sequence start (the hook is
// ds2/cutscene.cpp) with its resource, entity and network UUIDs, category and stop frame, and every change of the
// DSGameState bits (the cutscene states), for the live checks of analysis/STORY_SYNC_PLAN.md.
#include "ds2/cutscene_log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ds2/game_state.h"
#include "ds2/sim_tick.h"
#include "log.h"

namespace {

constexpr ULONGLONG kBitsPollMs = 250;
constexpr uintptr_t kFrame = 0x34C;

std::atomic<bool> g_enabled{false};

void tick() {
    static ULONGLONG last = 0;
    static uint64_t lastLow = ~0ull, lastHigh = ~0ull;
    const ULONGLONG now = GetTickCount64();
    if (now - last < kBitsPollMs) return;
    last = now;
    uint64_t low = 0, high = 0;
    if (!game_state::read(low, high)) return;
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

// The engine calls a pending start again every update, for every pending Sequence in turn: each Sequence's start is logged
// when its decision changes, not on every call (a live log held 275 000 such lines in 3 minutes).
constexpr size_t kMaxRemembered = 512;
std::mutex g_decisionsMutex;
std::unordered_map<uintptr_t, std::string> g_decisions;  // Sequence -> the decision logged last

bool decisionChanged(uintptr_t sequence, const char* decision) {
    std::lock_guard lock(g_decisionsMutex);
    if (g_decisions.size() >= kMaxRemembered) g_decisions.clear();
    std::string& last = g_decisions[sequence];
    if (last == decision) return false;
    last = decision;
    return true;
}

}  // namespace

namespace cutscene_log {

void enable() {
    g_enabled = true;
    sim_tick::add(&tick, "cutscene log");
}

void onStart(uintptr_t sequence, const sequence_info::Info& info, const char* decision) {
    if (!g_enabled || !decisionChanged(sequence, decision)) return;
    char resource[sequence_info::kUuidSize * 2 + 1], entity[sizeof(resource)], network[sizeof(resource)];
    hex(info.resource, resource);
    hex(info.entity, entity);
    hex(info.network, network);
    logger::write("cutscene_log: Sequence start %p %s frame %d stop %d category %u state %u resource %s entity %s network %s",
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
