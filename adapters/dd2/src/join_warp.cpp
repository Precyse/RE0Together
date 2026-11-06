#include "join_warp.h"

#include "join_warp_rule.h"
#include "local_body.h"
#include "log.h"
#include "peer_bodies.h"
#include "puppet_driver.h"
#include "session.h"
#include "world_gate.h"

namespace {

using world_gate_rule::Place;

join_warp_rule::OncePerLoad g_once;

}  // namespace

namespace join_warp {

void tick(uint64_t) {
    const auto hostPlace = session::hostPlace();
    const auto hostSlot = session::hostSlot();
    if (session::isHost() || !hostPlace || !hostSlot) return;
    g_once.observe(world_gate::loadSerial(), hostPlace->loadSerial);
    if (!g_once.pending() || world_gate::place() != Place::InGame || static_cast<Place>(hostPlace->place) != Place::InGame) return;
    const auto host = peer_bodies::latest(*hostSlot, wire_dd2::kKindArisen);
    const auto own = local_body::sample(wire_dd2::kKindArisen);
    if (!host || host->loadSerial != hostPlace->loadSerial || !own) return;
    g_once.settle();
    const double distance = join_warp_rule::distanceM(own->position, host->position);
    if (!join_warp_rule::needsWarp(distance)) {
        logger::write("join_warp: %.0f m from the host (load %u, host load %u), no warp", distance, world_gate::loadSerial(), hostPlace->loadSerial);
        return;
    }
    const bool warped = puppet_driver::warpTo(local_body::character(wire_dd2::kKindArisen), host->position, host->yaw);
    logger::write("join_warp: %.0f m from the host (load %u, host load %u), %s", distance, world_gate::loadSerial(), hostPlace->loadSerial,
                  warped ? "warped to it" : "warp refused");
}

}  // namespace join_warp
