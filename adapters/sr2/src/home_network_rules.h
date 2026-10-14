#pragma once
#include <cstdint>
#include <cstring>

// The pure rules of milestone 5d (unit tested): which join_req counts as a resend, and how the local address list is
// reordered.
namespace home_network_rules {

constexpr size_t kIpv4Bytes = 4;

// A join_req is a resend of a member's only when it comes from the member's real source address. Two machines on one
// home network share the public IP but never the source port.
inline bool sameRealSource(uint32_t memberIp, uint16_t memberPort, uint32_t senderIp, uint16_t senderPort) {
    return memberIp == senderIp && memberPort == senderPort;
}

// Puts `address` first in a null-terminated list of IPv4 addresses: the entry that equals it moves to the front, else
// the first entry is overwritten. An empty list stays empty.
inline void putFirst(char** list, const uint8_t (&address)[kIpv4Bytes]) {
    if (!list || !list[0]) return;
    for (size_t i = 0; list[i]; ++i) {
        if (memcmp(list[i], address, kIpv4Bytes) != 0) continue;
        char* found = list[i];
        list[i] = list[0];
        list[0] = found;
        return;
    }
    memcpy(list[0], address, kIpv4Bytes);
}

}  // namespace home_network_rules
