#include "lan_address.h"

// The thunk replaces the game's own gethostbyname calls, so it calls the same function.
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ascii_text.h"
#include "code_patch.h"
#include "lan_address_patches.h"
#include "lan_address_rules.h"
#include "log.h"

namespace {

constexpr ULONG kAdapterFlags =
    GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
constexpr int kAdapterReadTries = 3;
constexpr size_t kHostNameBytes = 256;

bool g_trace = false;
lan_address::OwnAddress g_virtualOwn = nullptr;
std::once_flag g_chosenOnce;
std::once_flag g_unknownOnce;
std::optional<uint32_t> g_chosen;

bool hasIpv4Gateway(const IP_ADAPTER_ADDRESSES& adapter) {
    for (const IP_ADAPTER_GATEWAY_ADDRESS_LH* gateway = adapter.FirstGatewayAddress; gateway; gateway = gateway->Next) {
        if (gateway->Address.lpSockaddr && gateway->Address.lpSockaddr->sa_family == AF_INET) return true;
    }
    return false;
}

lan_address_rules::Adapter describe(const IP_ADAPTER_ADDRESSES& a) {
    lan_address_rules::Adapter adapter;
    adapter.up = a.OperStatus == IfOperStatusUp;
    adapter.hasGateway = hasIpv4Gateway(a);
    adapter.ifType = a.IfType;
    adapter.metric = a.Ipv4Metric;
    adapter.names = ascii_text::narrow(a.FriendlyName) + " " + ascii_text::narrow(a.Description);
    for (const IP_ADAPTER_UNICAST_ADDRESS* u = a.FirstUnicastAddress; u; u = u->Next) {
        if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
        adapter.addresses.push_back(reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.S_un.S_addr);
    }
    return adapter;
}

std::vector<lan_address_rules::Adapter> readAdapters() {
    ULONG size = 0;
    std::vector<uint8_t> buffer;
    for (int attempt = 0; attempt < kAdapterReadTries; ++attempt) {
        buffer.resize(size);
        auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        const ULONG status = GetAdaptersAddresses(AF_INET, kAdapterFlags, nullptr, first, &size);
        if (status == ERROR_BUFFER_OVERFLOW) continue;
        if (status != NO_ERROR) return {};
        std::vector<lan_address_rules::Adapter> adapters;
        for (const IP_ADAPTER_ADDRESSES* a = first; a; a = a->Next) adapters.push_back(describe(*a));
        return adapters;
    }
    return {};
}

void logChoice(const std::vector<lan_address_rules::Adapter>& adapters, std::optional<uint32_t> chosen) {
    if (!chosen) {
        logger::write("trace: local address: no adapter qualifies (%zu read), the host name's list stays", adapters.size());
        return;
    }
    for (const auto& adapter : adapters) {
        if (std::find(adapter.addresses.begin(), adapter.addresses.end(), *chosen) == adapter.addresses.end()) continue;
        const auto* ip = reinterpret_cast<const uint8_t*>(&*chosen);
        logger::write("trace: local address %u.%u.%u.%u from adapter \"%s\" (metric %u)", ip[0], ip[1], ip[2], ip[3],
                      adapter.names.c_str(), adapter.metric);
        return;
    }
}

// Once per process: the address to put first, if any adapter qualifies.
std::optional<uint32_t> chosenAddress() {
    std::call_once(g_chosenOnce, [] {
        const auto adapters = readAdapters();
        g_chosen = lan_address_rules::choose(adapters);
        if (g_trace) logChoice(adapters, g_chosen);
    });
    return g_chosen;
}

bool isOwnHostName(const char* name) {
    char own[kHostNameBytes];
    return name && gethostname(own, sizeof(own)) == 0 && _stricmp(name, own) == 0;
}

// All five calls: the real gethostbyname, then for the machine's own name the virtual address (tunnel) or the chosen LAN
// address (adapter rule) first.
hostent* __stdcall lanHostByName(const char* name) {
    hostent* host = gethostbyname(name);
    if (!host || host->h_addrtype != AF_INET || host->h_length != sizeof(uint32_t)) return host;
    const bool ownName = isOwnHostName(name);
    if (g_virtualOwn) {
        const auto rule = lan_address_rules::putVirtual(ownName, ownName ? g_virtualOwn() : std::nullopt, host->h_addr_list);
        if (rule == lan_address_rules::VirtualRule::AddressUnknown) {
            std::call_once(g_unknownOnce, [] { logger::write("tunnel: own address asked before the launcher link"); });
        }
    } else if (ownName) {
        if (const auto address = chosenAddress()) lan_address_rules::putFirst(host->h_addr_list, *address);
    }
    return host;
}

}  // namespace

namespace lan_address {

bool install(uint32_t maxPlayers, bool trace) {
    g_trace = trace;
    const auto patches = lan_address_patches::forPlayers(maxPlayers, thunk());
    if (patches.empty()) return false;
    if (!applyCodePatches(patches)) {
        logger::write("lan address: sites differ, the host name's address list is not changed");
        return false;
    }
    if (trace) logger::write("lan address: %zu gethostbyname calls retargeted", patches.size());
    return true;
}

uintptr_t thunk() { return reinterpret_cast<uintptr_t>(&lanHostByName); }

void useVirtualRule(OwnAddress own) { g_virtualOwn = own; }

}  // namespace lan_address
