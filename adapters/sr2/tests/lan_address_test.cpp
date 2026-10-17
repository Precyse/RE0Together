// Milestone 5g, L1 (no game): the adapter choice, the address list swap, and the five call sites as data.
#include <algorithm>
#include <cstdio>
#include <optional>

#include "../src/lan_address_patches.h"
#include "../src/lan_address_rules.h"
#include "../src/partner_sites.h"
#include "../src/join_net_trace_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "juiced_targets.h"

namespace {

using lan_address_rules::Adapter;

constexpr uintptr_t kThunk = 0x10005000;
constexpr uintptr_t kOtherThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr size_t kCallBytes = 5;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

// An IPv4 address as the game stores it: the first octet is the lowest byte.
constexpr uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return a | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24;
}

Adapter lan(const char* names, uint32_t metric, uint32_t address) {
    Adapter adapter;
    adapter.up = true;
    adapter.hasGateway = true;
    adapter.ifType = lan_address_rules::kIfTypeEthernet;
    adapter.metric = metric;
    adapter.names = names;
    adapter.addresses = {address};
    return adapter;
}

bool overlaps(uintptr_t a, size_t aSize, uintptr_t b, size_t bSize) { return a < b + bSize && b < a + aSize; }

bool clearOfJuiced(const std::vector<CodePatch>& patches) {
    for (const CodePatch& patch : patches) {
        const uintptr_t first = patch.address - kJuicedMargin;
        const uintptr_t last = patch.address + patch.expected.size() + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            return false;
        }
    }
    return true;
}

bool clearOfOtherMilestones(const std::vector<CodePatch>& patches) {
    const player_cap_patches::Thunks thunks{kOtherThunk, kOtherThunk};
    const auto playerCap = player_cap_patches::forPlayers(kFourPlayers, thunks);
    for (const CodePatch& patch : patches) {
        const size_t size = patch.expected.size();
        for (const partner_sites::Site& site : partner_sites::kSites) {
            if (overlaps(patch.address, size, site.address, partner_sites::parseHex(site.expected).size())) return false;
        }
        for (const auto& entry : replication_sites::kEntries) {
            if (overlaps(patch.address, size, entry.n1, replication_sites::kEntryProbeBytes)) return false;
        }
        for (const auto& branch : replication_sites::kBranches) {
            if (overlaps(patch.address, size, branch.address, 2)) return false;
        }
        for (const CodePatch& other : playerCap) {
            if (overlaps(patch.address, size, other.address, other.expected.size())) return false;
        }
        for (const auto& trace : join_net_trace_sites::kSites) {
            if (overlaps(patch.address, size, trace.address, partner_sites::parseHex(trace.expected).size())) return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    using namespace lan_address_rules;

    const Adapter ethernet = lan("Ethernet Realtek PCIe GbE", 25, ip(192, 168, 1, 20));
    check(qualifies(ethernet), "an Ethernet adapter that is up, has a gateway and a LAN address qualifies");
    Adapter wifi = ethernet;
    wifi.ifType = kIfTypeWifi;
    check(qualifies(wifi), "Wi-Fi qualifies too");
    Adapter down = ethernet;
    down.up = false;
    Adapter noGateway = ethernet;
    noGateway.hasGateway = false;
    Adapter tunnel = ethernet;
    tunnel.ifType = 131;
    check(!qualifies(down) && !qualifies(noGateway) && !qualifies(tunnel),
          "an adapter that is down, has no gateway or is neither Ethernet nor Wi-Fi does not");
    check(excludedByName("Radmin VPN Ethernet Adapter") && excludedByName("ProtonVPN TUN") &&
              excludedByName("TAP-Windows Adapter V9") && excludedByName("vEthernet (WSL)") &&
              excludedByName("VMware Virtual Ethernet Adapter") && excludedByName("Hyper-V Virtual Ethernet Adapter") &&
              excludedByName("Npcap Loopback Adapter") && excludedByName("WireGuard Tunnel") &&
              excludedByName("Tailscale") && excludedByName("ZeroTier One") && excludedByName("Hamachi") &&
              excludedByName("OpenVPN Data Channel Offload") && excludedByName("VirtualBox Host-Only Ethernet Adapter"),
          "every VPN, tunnel and virtual adapter name of the list is excluded, in any case");
    check(!excludedByName("Intel(R) Wi-Fi 6 AX201 160MHz") && !excludedByName("Realtek PCIe GbE Family Controller") &&
              !excludedByName("Qualcomm Atheros QCA9377 Wireless Network Adapter"),
          "ordinary adapter names are not");

    check(choose({}) == std::nullopt, "no adapters: no choice");
    check(choose({down, noGateway}) == std::nullopt, "no qualifying adapter: no choice");
    check(choose({ethernet}) == ip(192, 168, 1, 20), "one adapter: its LAN address");
    const Adapter vpn = lan("ProtonVPN TUN", 1, ip(10, 2, 0, 2));
    check(choose({vpn, ethernet}) == ip(192, 168, 1, 20), "a VPN with a lower metric does not win");
    const Adapter second = lan("Wi-Fi", 40, ip(10, 0, 0, 7));
    const Adapter first = lan("Ethernet", 10, ip(192, 168, 1, 20));
    check(choose({second, first}) == ip(192, 168, 1, 20) && choose({first, second}) == ip(192, 168, 1, 20),
          "the lowest metric wins, whatever the order");
    Adapter publicOnly = lan("Ethernet", 5, ip(8, 8, 4, 4));
    check(choose({publicOnly, second}) == ip(10, 0, 0, 7), "an adapter without a LAN address is skipped");
    Adapter several = lan("Ethernet", 5, ip(169, 254, 3, 3));
    several.addresses.push_back(ip(172, 20, 1, 9));
    check(choose({several}) == ip(172, 20, 1, 9), "the first LAN address of the adapter is chosen, link-local is not one");

    uint8_t virtualAdapter[4] = {192, 168, 56, 1};
    uint8_t realAdapter[4] = {192, 168, 1, 20};
    uint8_t other[4] = {10, 0, 0, 5};
    char* list[] = {reinterpret_cast<char*>(virtualAdapter), reinterpret_cast<char*>(realAdapter), nullptr};
    check(putFirst(list, ip(192, 168, 1, 20)) && list[0] == reinterpret_cast<char*>(realAdapter) &&
              list[1] == reinterpret_cast<char*>(virtualAdapter) && !list[2],
          "the chosen address is swapped to the front");
    char* single[] = {reinterpret_cast<char*>(virtualAdapter), nullptr};
    check(!putFirst(single, ip(10, 0, 0, 5)) && virtualAdapter[0] == 192 && single[0] == reinterpret_cast<char*>(virtualAdapter),
          "an address that is not in the list changes nothing, no entry is overwritten");
    char* firstAlready[] = {reinterpret_cast<char*>(realAdapter), reinterpret_cast<char*>(other), nullptr};
    check(putFirst(firstAlready, ip(192, 168, 1, 20)) && firstAlready[0] == reinterpret_cast<char*>(realAdapter) &&
              firstAlready[1] == reinterpret_cast<char*>(other),
          "an address that is already first stays");
    char* empty[] = {nullptr};
    check(!putFirst(empty, ip(10, 0, 0, 5)) && !putFirst(nullptr, ip(10, 0, 0, 5)), "an empty or missing list stays as it is");

    const auto patches = lan_address_patches::forPlayers(kFourPlayers, kThunk);
    check(patches.size() == 5, "five calls");
    const std::vector<std::vector<uint8_t>> shipped = {{0xE8, 0xAB, 0x28, 0x41, 0x00}, {0xE8, 0x6F, 0xBB, 0x42, 0x00},
                                                       {0xE8, 0x01, 0x1A, 0x34, 0x00}, {0xE8, 0x3F, 0xB9, 0x34, 0x00},
                                                       {0xE8, 0x41, 0x74, 0x00, 0x00}};
    const uintptr_t addresses[] = {0x810C94, 0x7F79D0, 0x8E1B3E, 0x8D7C00, 0xC1C0FE};
    bool bytesMatch = patches.size() == 5;
    for (size_t i = 0; bytesMatch && i < patches.size(); ++i) {
        const uint32_t to = static_cast<uint32_t>(kThunk - (addresses[i] + kCallBytes));
        const std::vector<uint8_t> written = {0xE8, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8),
                                              static_cast<uint8_t>(to >> 16), static_cast<uint8_t>(to >> 24)};
        bytesMatch = patches[i].address == addresses[i] && patches[i].expected == shipped[i] && patches[i].replacement == written;
    }
    check(bytesMatch, "each call: the exe's bytes (all to the gethostbyname stub) are checked, the target becomes the thunk");
    check(lan_address_patches::forPlayers(2, kThunk).empty(), "max_players 2 (the shipped co-op) patches nothing");
    // The Steam tunnel's rule: the own name gets the virtual address, written over the first entry (network order).
    constexpr uint32_t kVirtual = 0x0A0B0C0D;
    uint8_t own[4] = {192, 168, 1, 20};
    uint8_t listed[4] = {192, 168, 56, 1};
    char* ownList[] = {reinterpret_cast<char*>(own), reinterpret_cast<char*>(listed), nullptr};
    check(putVirtual(true, kVirtual, ownList) == VirtualRule::Replaced && own[0] == 10 && own[1] == 11 && own[2] == 12 && own[3] == 13 &&
              listed[0] == 192 && ownList[0] == reinterpret_cast<char*>(own),
          "the own name: the first address becomes 10.11.12.13, the rest of the list stays");
    uint8_t other_name[4] = {93, 184, 216, 34};
    char* otherList[] = {reinterpret_cast<char*>(other_name), nullptr};
    check(putVirtual(false, kVirtual, otherList) == VirtualRule::OtherName && other_name[0] == 93,
          "any other name is left alone");
    uint8_t before[4] = {192, 168, 1, 20};
    char* waiting[] = {reinterpret_cast<char*>(before), nullptr};
    check(putVirtual(true, std::nullopt, waiting) == VirtualRule::AddressUnknown && before[0] == 192 && before[3] == 20,
          "before the own address is known nothing is changed");
    char* nothing[] = {nullptr};
    check(putVirtual(true, kVirtual, nothing) == VirtualRule::OtherName && putVirtual(true, kVirtual, nullptr) == VirtualRule::OtherName,
          "an empty or missing list stays as it is");

    const auto tunnelPatches = lan_address_patches::forTunnel(kThunk);
    check(tunnelPatches.size() == 5 && tunnelPatches[0].address == patches[0].address &&
              tunnelPatches[4].replacement == patches[4].replacement,
          "the tunnel writes the same five calls at any player count");
    check(clearOfJuiced(patches), "no call within 16 bytes of an address Juiced touches");
    check(clearOfOtherMilestones(patches), "no call overlaps a milestone 3, 4, 5 or 5b site");
    return g_failures == 0 ? 0 : 1;
}
