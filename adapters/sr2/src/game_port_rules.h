#pragma once
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of milestone 5e (unit tested): which game ports are valid, and which port a peer is reached at.
namespace game_port_rules {

constexpr uint32_t kShippedGamePort = sr2::kGamePort;  // what the game binds and every joiner assumes for the host
constexpr uint32_t kMinGamePort = 1024;
constexpr uint32_t kMaxGamePort = 65535;

inline bool valid(unsigned long port) { return port >= kMinGamePort && port <= kMaxGamePort; }

// The port the same-public-IP quick check uses for a peer: the host is always on the shipped port, anyone else on the
// port the host relayed (its source port, its own game_port when its router keeps it). Without a relayed port the
// shipped one stays.
inline uint16_t peerPort(bool peerIsHost, uint16_t relayedPort) {
    return peerIsHost || relayedPort == 0 ? static_cast<uint16_t>(kShippedGamePort) : relayedPort;
}

}  // namespace game_port_rules
