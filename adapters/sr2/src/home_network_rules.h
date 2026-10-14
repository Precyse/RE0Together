#pragma once
#include <cstdint>

// The pure rule of milestone 5d (unit tested): which join_req counts as a resend.
namespace home_network_rules {

// A join_req is a resend of a member's only when it comes from the member's real source address. Two machines on one
// home network share the public IP but never the source port.
inline bool sameRealSource(uint32_t memberIp, uint16_t memberPort, uint32_t senderIp, uint16_t senderPort) {
    return memberIp == senderIp && memberPort == senderPort;
}

}  // namespace home_network_rules
