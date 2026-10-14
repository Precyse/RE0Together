#include "home_network.h"

// The thunk replaces the game's own gethostbyname calls, so it calls the same function.
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <vector>

#include "home_network_rules.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

// An address that is never contacted: only the route to it is looked up.
constexpr const char* kRouteProbeAddress = "8.8.8.8";
constexpr ULONG kAdapterFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
constexpr int kAdapterReadTries = 3;

bool g_trace = false;
const uintptr_t g_findDuplicateMember = sr2::kFindDuplicateMember;

// The interface the default route to the internet leaves by.
bool defaultRouteInterface(DWORD& index) {
    in_addr probe{};
    if (inet_pton(AF_INET, kRouteProbeAddress, &probe) != 1) return false;
    return GetBestInterface(probe.S_un.S_addr, &index) == NO_ERROR;
}

// The first IPv4 unicast address of the interface `index`.
bool interfaceAddress(DWORD index, uint8_t (&address)[home_network_rules::kIpv4Bytes]) {
    ULONG size = 0;
    std::vector<uint8_t> buffer;
    for (int attempt = 0; attempt < kAdapterReadTries; ++attempt) {
        buffer.resize(size);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        const ULONG status = GetAdaptersAddresses(AF_INET, kAdapterFlags, nullptr, adapters, &size);
        if (status == ERROR_BUFFER_OVERFLOW) continue;
        if (status != NO_ERROR) return false;
        for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->IfIndex != index || !adapter->FirstUnicastAddress) continue;
            const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(adapter->FirstUnicastAddress->Address.lpSockaddr);
            memcpy(address, &socketAddress->sin_addr, sizeof(address));
            return true;
        }
        return false;
    }
    return false;
}

// D1: the search's member counts only when its real source address is the sender's.
uint32_t __cdecl duplicateJoinSender(uint32_t member, uintptr_t sender) {
    if (!member) return 0;
    const uint32_t memberIp = readDword(member + sr2::kMemberSourceIpOffset);
    uint16_t memberPort = 0;
    readMemory(member + sr2::kMemberSourcePortOffset, &memberPort, sizeof(memberPort));
    const uint32_t senderIp = readDword(sender);
    uint16_t senderPort = 0;
    readMemory(sender + sr2::kSenderPortOffset, &senderPort, sizeof(senderPort));
    const bool resend = home_network_rules::sameRealSource(memberIp, memberPort, senderIp, senderPort);
    if (g_trace) {
        logger::write("trace: join_req duplicate search found member 0x%X, source 0x%08X:%u, sender 0x%08X:%u, %s",
                      static_cast<unsigned>(member), memberIp, memberPort, senderIp, senderPort,
                      resend ? "a resend, dropped" : "another machine, accepted");
    }
    return resend ? member : 0;
}

// The search finds the member with eax = session and esi = sender; the sender block is kept for the decision.
void __declspec(naked) duplicateJoinSenderStub() {
    __asm {
        push esi
        call dword ptr [g_findDuplicateMember]
        pop edx
        push edx
        push eax
        call duplicateJoinSender
        add esp, 8
        ret
    }
}

// D2, D2b: gethostbyname with the default route interface's IPv4 first.
hostent* __stdcall primaryHostByName(const char* name) {
    hostent* host = gethostbyname(name);
    if (!host || host->h_addrtype != AF_INET || host->h_length != home_network_rules::kIpv4Bytes) return host;
    DWORD index = 0;
    uint8_t address[home_network_rules::kIpv4Bytes];
    if (!defaultRouteInterface(index) || !interfaceAddress(index, address)) {
        if (g_trace) logger::write("trace: local address: no default route interface, the host name's first address stays");
        return host;
    }
    home_network_rules::putFirst(host->h_addr_list, address);
    if (g_trace) {
        logger::write("trace: local address %u.%u.%u.%u (default route interface %lu)", address[0], address[1], address[2],
                      address[3], index);
    }
    return host;
}

}  // namespace

namespace home_network {

void setTrace(bool trace) { g_trace = trace; }

uintptr_t duplicateJoinSenderThunk() { return reinterpret_cast<uintptr_t>(&duplicateJoinSenderStub); }

uintptr_t hostByNameThunk() { return reinterpret_cast<uintptr_t>(&primaryHostByName); }

}  // namespace home_network
