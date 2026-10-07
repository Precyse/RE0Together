#include "room_gate.h"

#include <atomic>
#include <chrono>

#include "debug_overlay.h"
#include "debug_stats.h"
#include "door_sync.h"
#include "door_travel.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "room_gate_rule.h"

namespace {

using Clock = std::chrono::steady_clock;
using FinishCheckFunction = bool(__fastcall*)(void* door, void* edx);

constexpr float kWaitingToastSeconds = 2.0f;

FinishCheckFunction g_originalFinishCheck = nullptr;
std::atomic<bool> g_ready{false};  // this machine's door is ready to finish
bool g_holding = false;            // game thread only
bool g_waitingShown = false;       // game thread only
Clock::time_point g_since;         // game thread only

bool peer(room_gate_rule::Peer& out) {
    door_travel::RoomState report{};
    const bool known = net_pad::active() && door_travel::peerReport(report);
    out = {report.scene, report.doorTarget, door_travel::doorShared(report), door_travel::doorReady(report)};
    return known;
}

uint16_t targetOf(void* door) {
    uint32_t room = 0;
    game::readMemory(reinterpret_cast<uintptr_t>(door) + game::kDoorLoadRoomOffset, room);
    return static_cast<uint16_t>(room);
}

int64_t heldMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_since).count();
}

// The door finishes now; the next door starts unready.
bool finish() {
    g_holding = false;
    g_waitingShown = false;
    g_ready = false;
    return true;
}

bool startHold(uint16_t here) {
    room_gate_rule::Peer report{};
    if (!peer(report) || !room_gate_rule::holdsAtFinish(door_sync::sharedDoor(), here, report)) return false;
    g_holding = true;
    g_since = Clock::now();
    debug_stats::count(debug_stats::Counter::RoomGateHolds);
    logger::write("room_gate: door into scene 0x%02x waits for the peer's", here);
    return true;
}

// True when the door into `here` may finish now.
bool mayFinish(uint16_t here) {
    if (!g_ready.exchange(true)) door_travel::announce();  // the peer may be waiting for exactly this
    if (!g_holding && !startHold(here)) return finish();
    room_gate_rule::Peer report{};
    const bool known = peer(report);
    const int64_t held = heldMs();
    const room_gate_rule::Verdict verdict = room_gate_rule::check(here, known, report, held);
    if (verdict == room_gate_rule::Verdict::Hold) {
        if (!g_waitingShown && room_gate_rule::showsWaiting(held)) {
            g_waitingShown = true;
            debug_overlay::toast("Waiting for partner", kWaitingToastSeconds);
        }
        return false;
    }
    logger::write("room_gate: door into scene 0x%02x released (%s) after %lld ms", here, room_gate_rule::name(verdict),
                  static_cast<long long>(held));
    return finish();
}

// The door's own check (its animation done) comes first; ours only ever defers a door that would finish.
bool __fastcall finishCheckDetour(void* door, void* edx) {
    if (!g_originalFinishCheck(door, edx)) return false;
    if (!net_pad::active()) return finish();
    return mayFinish(targetOf(door));
}

}  // namespace

namespace room_gate {

bool install() {
    return hooks::install("door finish check", game::kDoorFinishCheckFunction,
                          reinterpret_cast<void*>(finishCheckDetour), reinterpret_cast<void**>(&g_originalFinishCheck));
}

bool doorReady() { return g_ready; }

}  // namespace room_gate
