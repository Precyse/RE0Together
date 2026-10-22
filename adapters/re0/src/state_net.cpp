#include "state_net.h"

#include "debug_stats.h"
#include "log.h"
#include "protocol.h"
#include "state_mode.h"
#include "state_net_rule.h"

namespace {

NetClient* g_net = nullptr;

}  // namespace

namespace state_net {

void bind(NetClient& net) { g_net = &net; }

bool send(uint16_t type, bool reliable, std::span<const uint8_t> payload) {
    if (!state_net_rule::allowed(state_mode::on(), type)) {
        debug_stats::count(debug_stats::Counter::StateSendRefused);
        logger::writeUnlessRepeated("state_net: refused type 0x%04x outside state mode", type);
        return false;
    }
    return g_net && g_net->send(type, reliable, proto::kSlotAll, payload);
}

}  // namespace state_net
