#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "local_port_rules.h"

// The pure rules of milestone 5g, L1 (unit tested): which network adapter's IPv4 address the game should use as its local
// address, and how it is put first in the host name's address list.
namespace lan_address_rules {

constexpr uint32_t kIfTypeEthernet = 6;
constexpr uint32_t kIfTypeWifi = 71;

// Adapters of VPNs, tunnels, virtual machines and containers: no other machine on the LAN can be reached through them.
inline constexpr const char* kExcludedNames[] = {"radmin",  "hamachi",    "zerotier", "tailscale", "wireguard",
                                                 "openvpn", "tap",        "proton",   "virtualbox", "vmware",
                                                 "hyper-v", "vethernet",  "wsl",      "npcap"};

// What the choice needs of an adapter (read from GetAdaptersAddresses).
struct Adapter {
    bool up = false;
    bool hasGateway = false;  // an IPv4 default gateway
    uint32_t ifType = 0;
    uint32_t metric = 0;      // IPv4 metric, the lowest wins
    std::string names;        // friendly name and description
    std::vector<uint32_t> addresses;  // IPv4 unicast, as stored in memory
};

inline std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline bool excludedByName(const std::string& names) {
    const std::string text = lowered(names);
    return std::any_of(std::begin(kExcludedNames), std::end(kExcludedNames),
                       [&](const char* excluded) { return text.find(excluded) != std::string::npos; });
}

inline bool qualifies(const Adapter& adapter) {
    return adapter.up && adapter.hasGateway && (adapter.ifType == kIfTypeEthernet || adapter.ifType == kIfTypeWifi) &&
           !excludedByName(adapter.names);
}

// The first RFC1918 address of the qualifying adapter with the lowest metric (the first of equals); none when no adapter
// qualifies.
inline std::optional<uint32_t> choose(const std::vector<Adapter>& adapters) {
    const Adapter* best = nullptr;
    uint32_t address = 0;
    for (const Adapter& adapter : adapters) {
        if (!qualifies(adapter) || (best && adapter.metric >= best->metric)) continue;
        const auto found = std::find_if(adapter.addresses.begin(), adapter.addresses.end(), local_port_rules::isPrivateIpv4);
        if (found == adapter.addresses.end()) continue;
        best = &adapter;
        address = *found;
    }
    return best ? std::optional<uint32_t>(address) : std::nullopt;
}

// Puts `address` first in a null-terminated list of IPv4 addresses by swapping it with the first entry. A list that does
// not hold it stays as it is. True when the address is first afterwards.
inline bool putFirst(char** list, uint32_t address) {
    if (!list || !list[0]) return false;
    for (size_t i = 0; list[i]; ++i) {
        uint32_t entry = 0;
        memcpy(&entry, list[i], sizeof(entry));
        if (entry != address) continue;
        std::swap(list[0], list[i]);
        return true;
    }
    return false;
}

// The Steam tunnel's rule (docs/SR2_NOTES.md "Steam tunnel"): the machine's own name gets the virtual address (host order)
// as its first address, and any other name is left alone.
enum class VirtualRule { OtherName, AddressUnknown, Replaced };

inline VirtualRule putVirtual(bool ownName, std::optional<uint32_t> address, char** list) {
    if (!ownName || !list || !list[0]) return VirtualRule::OtherName;
    if (!address) return VirtualRule::AddressUnknown;
    const uint8_t octets[sizeof(uint32_t)] = {static_cast<uint8_t>(*address >> 24), static_cast<uint8_t>(*address >> 16),
                                              static_cast<uint8_t>(*address >> 8), static_cast<uint8_t>(*address)};
    memcpy(list[0], octets, sizeof(octets));
    return VirtualRule::Replaced;
}

}  // namespace lan_address_rules
