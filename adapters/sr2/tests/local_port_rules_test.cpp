// The pure rules of milestone 5g (no game): the table of guests' local ports, private address ranges, report once.
#include <cstdio>

#include "../src/local_port_rules.h"
#include "check.h"

namespace {

constexpr uint32_t kPublicIp = 0x6A66E6AE;   // 174.230.102.106 as stored in memory
constexpr uint16_t kExternalPortOfSecond = 11948;
constexpr uint16_t kExternalPortOfThird = 4201;
constexpr uint16_t kLocalPort = 4200;

// An IPv4 address as the game stores it: the first octet is the lowest byte.
constexpr uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return a | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24;
}

}  // namespace

int main() {
    using namespace local_port_rules;

    PortMap ports;
    check(!ports.find(kPublicIp, kExternalPortOfSecond), "an unknown source has no local port");
    ports.remember(kPublicIp, kExternalPortOfSecond, kLocalPort);
    ports.remember(kPublicIp, kExternalPortOfThird, kLocalPort + 1);
    check(ports.find(kPublicIp, kExternalPortOfSecond) == kLocalPort && ports.find(kPublicIp, kExternalPortOfThird) == kLocalPort + 1,
          "two guests behind one public IP are told apart by their source port");
    check(!ports.find(kPublicIp + 1, kExternalPortOfSecond), "another IP is not the same source");
    ports.remember(kPublicIp, kExternalPortOfSecond, kLocalPort + 7);
    check(ports.find(kPublicIp, kExternalPortOfSecond) == kLocalPort + 7, "a repeated join_req updates the entry");
    ports.remember(kPublicIp, 1, 0);
    check(!ports.find(kPublicIp, 1), "a reported port of 0 is not kept");

    PortMap full;
    for (uint32_t i = 0; i < kMaxSources + 1; ++i) full.remember(i + 1, 1, static_cast<uint16_t>(i + 1));
    check(!full.find(1, 1) && full.find(2, 1) == 2 && full.find(kMaxSources + 1, 1) == kMaxSources + 1,
          "a full table forgets the oldest source first");

    check(isPrivateIpv4(ip(10, 1, 2, 3)) && isPrivateIpv4(ip(192, 168, 1, 227)), "10/8 and 192.168/16 are private");
    check(isPrivateIpv4(ip(172, 16, 0, 1)) && isPrivateIpv4(ip(172, 31, 255, 255)), "172.16/12 is private");
    check(!isPrivateIpv4(ip(172, 15, 0, 1)) && !isPrivateIpv4(ip(172, 32, 0, 1)), "172.15 and 172.32 are not");
    check(!isPrivateIpv4(ip(192, 169, 1, 1)) && !isPrivateIpv4(ip(11, 0, 0, 1)) && !isPrivateIpv4(kPublicIp),
          "other addresses are public");

    SeenAddresses seen;
    check(seen.firstTime(ip(192, 168, 1, 227), 4200), "an address is new the first time");
    check(!seen.firstTime(ip(192, 168, 1, 227), 4200), "and reported once");
    check(seen.firstTime(ip(192, 168, 1, 227), 4201), "another port is another address");
    return g_failures == 0 ? 0 : 1;
}
