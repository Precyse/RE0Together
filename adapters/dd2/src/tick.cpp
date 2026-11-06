#include "tick.h"

#include <windows.h>

#include <cstdint>

#include "crash_dump.h"
#include "body_card.h"
#include "echo_puppet.h"
#include "enemy_mirror.h"
#include "enemy_stream.h"
#include "peer_puppet.h"
#include "log.h"
#include "refw.h"
#include "offline.h"
#include "offline_capture.h"
#include "owner_stream.h"
#include "peer_bodies.h"
#include "plague_off.h"
#include "profile.h"
#include "puppet_brain.h"
#include "roster.h"
#include "save_redirect.h"
#include "session.h"
#include "world_clock.h"
#include "world_gate.h"

namespace {

constexpr const char* kUpdateEntry = "UpdateBehavior";
constexpr const char* kLateUpdateEntry = "LateUpdateBehavior";
constexpr uint64_t kStepMs = 33;  // 30 Hz
constexpr int kMaxCatchUpSteps = 4;

DWORD g_updateThread = 0, g_lateThread = 0;
uint64_t g_nextStepMs = 0;

bool gameInForeground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void runSteps() {
    const uint64_t nowMs = GetTickCount64();
    if (!g_nextStepMs) g_nextStepMs = nowMs;
    int steps = 0;
    while (nowMs >= g_nextStepMs && steps < kMaxCatchUpSteps) {
        save_redirect::tick(nowMs);
        session::tick(nowMs);
        profile::tick(nowMs);
        offline_capture::tick(nowMs);
        plague_off::tick(nowMs);
        roster::tick(nowMs);
        offline::tick(nowMs);
        echo_puppet::tick(nowMs);
        body_card::tick(nowMs);    // also drops the cards of peers that left
        peer_puppet::tick(nowMs);  // also removes the puppets of peers that left
        enemy_stream::tick(nowMs);
        enemy_mirror::tick(nowMs);  // also lets its mirrors go outside a session
        puppet_brain::tick(nowMs);
        if (session::active()) {
            owner_stream::tick(nowMs);
            world_clock::tick(nowMs);
            peer_bodies::tick(nowMs);
        }
        g_nextStepMs += kStepMs;
        ++steps;
    }
    if (nowMs >= g_nextStepMs) g_nextStepMs = nowMs + kStepMs;  // a long stall: drop the backlog
}

void runGuarded() {
    __try {
        runSteps();
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "tick")) {
    }
}

void onUpdate() {
    const DWORD thread = GetCurrentThreadId();
    if (thread != g_updateThread) {
        logger::write("tick: %s runs on thread %lu%s", kUpdateEntry, thread, g_updateThread ? " (thread changed)" : "");
        if (!g_updateThread) crash_dump::startHangWatch(gameInForeground);
        g_updateThread = thread;
        world_gate::setGameThread(thread);
    }
    crash_dump::heartbeat(thread);
    runGuarded();
}

void onLateUpdate() {
    const DWORD thread = GetCurrentThreadId();
    if (thread == g_lateThread) return;
    logger::write("tick: %s runs on thread %lu (%s)", kLateUpdateEntry, thread,
                  thread == g_updateThread ? "same as UpdateBehavior" : "NOT the UpdateBehavior thread");
    g_lateThread = thread;
}

}  // namespace

namespace tick {

bool install() {
    const REFrameworkPluginFunctions& functions = *refw::param()->functions;
    const bool update = functions.on_pre_application_entry(kUpdateEntry, onUpdate);
    const bool late = functions.on_post_application_entry(kLateUpdateEntry, onLateUpdate);
    logger::write("tick: %s", update && late ? "2 entries registered" : "not registered (REFramework refused an entry)");
    return update && late;
}

}  // namespace tick
