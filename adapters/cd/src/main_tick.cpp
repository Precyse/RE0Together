#include "main_tick.h"

#include <windows.h>

#include <cstdint>
#include <vector>

#include "build_gate.h"
#include "cd_sites.h"
#include "census.h"
#include "crash_dump.h"
#include "dev_cmd.h"
#include "equipment.h"
#include "funnels.h"
#include "game_state.h"
#include "hooks.h"
#include "link.h"
#include "minimap.h"
#include "log.h"
#include "presence_recv.h"
#include "presence_send.h"
#include "world_gate.h"
#include "save_guard.h"
#include "story.h"
#include "world_time.h"

namespace {

// Forwarded with eight integer arguments, as the funnels are: the real ones plus caller stack slots the callee ignores.
using ProcessRecvQueue = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

constexpr uint64_t kRateLogMs = 60000;

ProcessRecvQueue g_original = nullptr;
bool g_devCommands = false;
DWORD g_thread = 0;
uint64_t g_calls = 0, g_rateSinceMs = 0, g_rateCalls = 0;

bool gameInForeground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void noteThread(uint64_t nowMs) {
    const DWORD thread = GetCurrentThreadId();
    if (thread != g_thread) {
        logger::write("main_tick: runs on thread %lu%s", thread, g_thread ? " (thread changed)" : "");
        if (!g_thread) crash_dump::startHangWatch(gameInForeground);
        g_thread = thread;
    }
    crash_dump::heartbeat(thread);
    ++g_calls;
    if (!g_rateSinceMs) g_rateSinceMs = nowMs;
    if (nowMs - g_rateSinceMs >= kRateLogMs) {
        logger::writeUnlessRepeated("main_tick: %llu calls in the last minute", static_cast<unsigned long long>(g_calls - g_rateCalls));
        g_rateCalls = g_calls;
        g_rateSinceMs = nowMs;
    }
}

void runModules() {
    const uint64_t nowMs = GetTickCount64();
    noteThread(nowMs);
    for (const funnels::Event& event : funnels::takeEvents()) {
        world_gate::onEvent(event, nowMs);
        presence_send::onEvent(event, nowMs);
        equipment::onEvent(event);
        world_time::onEvent(event);
        story::onEvent(event, nowMs);
    }
    world_gate::tick(nowMs);
    link::tick(nowMs);
    const bool peerJoined = link::peerJoinedThisTick();
    presence_send::tick(nowMs, peerJoined);
    const std::vector<uint8_t> slots = link::peerSlots();
    presence_recv::tick(nowMs, slots);
    equipment::tick(nowMs, peerJoined, slots);
    minimap::tick(nowMs, slots);
    world_time::tick(nowMs);
    story::tick(nowMs, peerJoined);
    save_guard::tick(nowMs);
    game_state::tick();
    census::tick(nowMs);
    if (g_devCommands) dev_cmd::tick(nowMs);
}

void runModulesGuarded() {
    __try {
        runModules();
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "main_tick")) {
    }
}

uint64_t __fastcall detour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
                           uint64_t a8) {
    const uint64_t result = g_original(a1, a2, a3, a4, a5, a6, a7, a8);
    runModulesGuarded();
    return result;
}

}  // namespace

namespace main_tick {

bool install(bool devCommands) {
    g_devCommands = devCommands;
    if (!build_gate::setIntact(cd_sites::kTick)) {
        logger::write("tick: not patched (site bytes differ)");
        return false;
    }
    const bool ok = hooks::install(cd_sites::kProcessRecvQueue.name, cd_sites::kProcessRecvQueue.address,
                                   reinterpret_cast<void*>(&detour), reinterpret_cast<void**>(&g_original));
    logger::write(ok ? "tick: 1 patched" : "tick: not patched (hook install failed)");
    return ok;
}

}  // namespace main_tick
