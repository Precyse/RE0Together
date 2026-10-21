#include "world_epoch.h"

#include <atomic>

#include "character_owner.h"
#include "log.h"
#include "state_mode.h"
#include "world_epoch_rule.h"

namespace {

std::atomic<uint16_t> g_hostEpoch{world_epoch_rule::kFirstEpoch};
std::atomic<uint16_t> g_adopted{world_epoch_rule::kNone};

}  // namespace

namespace world_epoch {

void onLocalLoad() {
    g_hostEpoch = world_epoch_rule::next(g_hostEpoch.load());
    g_adopted = world_epoch_rule::kNone;
}

void adopt(uint16_t epoch) { g_adopted = epoch; }

uint16_t hostEpoch() { return g_hostEpoch.load(); }

uint16_t stamp() { return character_owner::isHost() ? g_hostEpoch.load() : g_adopted.load(); }

bool acceptsFromGuest(uint16_t stamp, const char* kind) {
    if (!state_mode::on() || !character_owner::isHost() || world_epoch_rule::current(stamp, g_hostEpoch.load())) return true;
    logger::writeUnlessRepeated("world_epoch: dropped a %s from before the last load (stamp %u, world %u)", kind, stamp,
                                g_hostEpoch.load());
    return false;
}

}  // namespace world_epoch
