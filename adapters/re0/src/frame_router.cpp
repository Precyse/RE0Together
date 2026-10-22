#include "frame_router.h"

#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "log.h"
#include "state_net_rule.h"

namespace {

struct Route {
    uint16_t type;
    frame_router::SenderPolicy policy;
    frame_router::Handler handler;
};

std::vector<Route> g_routes;  // written before the net thread starts, read only afterwards

const Route* find(uint16_t type) {
    for (const Route& route : g_routes) {
        if (route.type == type) return &route;
    }
    return nullptr;
}

}  // namespace

namespace frame_router {

bool add(uint16_t type, SenderPolicy policy, Handler handler) {
    if (!state_net_rule::isStateType(type) || find(type)) {
        logger::write("router: cannot register type 0x%04x", type);
        return false;
    }
    g_routes.push_back({type, policy, handler});
    return true;
}

bool dispatch(const GameFrame& frame, uint8_t hostSlot) {
    const Route* route = find(frame.type);
    if (!route) return false;
    if (!frame_router_rule::admits(route->policy, frame.slot, hostSlot, character_owner::isHost())) {
        debug_stats::count(debug_stats::Counter::RouterDropped);
        logger::writeUnlessRepeated("router: dropped type 0x%04x from slot %d (%s)", frame.type, frame.slot,
                                    frame_router_rule::name(route->policy));
        return true;
    }
    route->handler(frame);
    return true;
}

}  // namespace frame_router
