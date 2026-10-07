#include "room_gate.h"

#include <chrono>

#include "debug_stats.h"
#include "door_travel.h"
#include "log.h"
#include "net_pad.h"
#include "room_gate_rule.h"

namespace {

using Clock = std::chrono::steady_clock;

bool g_holding = false;  // game thread only
uint16_t g_scene = 0;    // game thread only: the room held
Clock::time_point g_since;

bool peer(room_gate_rule::Peer& out) {
    door_travel::RoomState report{};
    const bool known = net_pad::active() && door_travel::peerReport(report);
    out = {report.scene, report.doorTarget};
    return known;
}

}  // namespace

namespace room_gate {

void onArrival(uint16_t scene) {
    room_gate_rule::Peer report{};
    if (!peer(report) || !room_gate_rule::holdsAtArrival(scene, report)) return;
    g_holding = true;
    g_scene = scene;
    g_since = Clock::now();
    debug_stats::count(debug_stats::Counter::RoomGateHolds);
    logger::write("room_gate: holding scene 0x%02x for the peer (peer scene 0x%x)", scene, report.scene);
}

bool holdsWorld() {
    if (!g_holding) return false;
    room_gate_rule::Peer report{};
    const bool known = peer(report);
    const auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_since).count();
    const room_gate_rule::Verdict verdict = room_gate_rule::check(g_scene, known, report, heldMs);
    if (verdict == room_gate_rule::Verdict::Hold) return true;
    g_holding = false;
    logger::write("room_gate: released scene 0x%02x (%s) after %lld ms", g_scene, room_gate_rule::name(verdict),
                  static_cast<long long>(heldMs));
    return false;
}

}  // namespace room_gate
