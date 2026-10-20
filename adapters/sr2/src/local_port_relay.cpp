#include "local_port_relay.h"

#include <cstring>
#include <mutex>
#include <optional>

#include "local_port_rules.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

// Offsets from the stack pointer of the join_req handler at H1 (the `call` of the thunk is not counted).
constexpr uintptr_t kContextSlot = 0x14;      // pointer to the pointer to the sender block
constexpr uintptr_t kReportedPortSlot = 0x28; // word: the guest's reported local port, until 0x815D97 overwrites it

std::mutex g_mutex;
local_port_rules::PortMap g_ports;
local_port_rules::SeenAddresses g_seen;
bool g_trace = false;
const uintptr_t g_buildAddressRecord = sr2::kBuildAddressRecord;

// H1: `espAtSite` is the handler's stack pointer. The sender block is the real source of the join_req.
void __cdecl captureReportedPort(uintptr_t espAtSite) {
    const uintptr_t sender = readDword(readDword(espAtSite + kContextSlot));
    const uint32_t ip = readDword(sender + sr2::kSenderIpOffset);
    const uint16_t port = readWord(sender + sr2::kSenderPortOffset);
    const uint16_t local = readWord(espAtSite + kReportedPortSlot);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_ports.remember(ip, port, local);
    }
    if (g_trace) logger::write("trace: join_req from 0x%08X:%u reports local port %u", ip, port, local);
}

// H2: an address record for the member whose source address is at `source` was just written at `buffer + before`. The
// port inside it becomes the guest's local port, when the host knows it; the member's own record keeps the real source.
void __cdecl relayLocalPort(uintptr_t source, uintptr_t buffer, uint32_t before) {
    const uint32_t ip = readDword(source + sr2::kSenderIpOffset);
    const uint16_t port = readWord(source + sr2::kSenderPortOffset);
    std::optional<uint16_t> local;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        local = g_ports.find(ip, port);
    }
    if (!local) return;
    memcpy(reinterpret_cast<void*>(buffer + before + local_port_rules::kRecordPortOffset), &*local, sizeof(*local));
    if (g_trace) logger::write("trace: address record of 0x%08X:%u carries its local port %u", ip, port, *local);
}

// T11: the receive loop's sender lookup, then a note for a sender it does not know that comes from a private range.
uintptr_t __cdecl traceUnmappedSender(uintptr_t address, uint32_t kind) {
    const uintptr_t member = reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t, uint32_t)>(sr2::kMapSender)(address, kind);
    if (member != 0) return member;
    const uint32_t ip = readDword(address + sr2::kSenderIpOffset);
    const uint16_t port = readWord(address + sr2::kSenderPortOffset);
    bool first = false;
    if (local_port_rules::isPrivateIpv4(ip)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        first = g_seen.firstTime(ip, port);
    }
    if (first) {
        logger::write("trace: unmapped sender %u.%u.%u.%u:%u (message type %u)", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
                      ip >> 24, port, kind & 0xFF);
    }
    return member;
}

// Replaces `mov eax, [esp+0x14]; mov eax, [eax]` of the join_req handler: the capture, then those two instructions.
void __declspec(naked) reportedPortStub() {
    __asm {
        pushfd
        pushad
        lea eax, [esp + 40]  // the handler's stack pointer: pushad 32, pushfd 4, return address 4
        push eax
        call captureReportedPort
        add esp, 4
        popad
        popfd
        mov eax, dword ptr [esp + 4 + 0x14]  // kContextSlot behind the return address
        mov eax, dword ptr [eax]
        ret
    }
}

// Replaces the call of the record builder's address writer (eax = the offset, ecx = the buffer, esi = the source address).
// The writer keeps eax, ecx, esi and ebx; the rest of the builder reloads edx.
void __declspec(naked) relayedRecordPortStub() {
    __asm {
        movzx edx, word ptr [eax]
        push edx
        call dword ptr [g_buildAddressRecord]
        pop edx
        push eax
        push ecx
        push edx
        push ecx
        push esi
        call relayLocalPort
        add esp, 12
        pop ecx
        pop eax
        ret
    }
}

}  // namespace

namespace local_port_relay {

Thunks thunks() {
    return {reinterpret_cast<uintptr_t>(&reportedPortStub), reinterpret_cast<uintptr_t>(&relayedRecordPortStub),
            reinterpret_cast<uintptr_t>(&traceUnmappedSender)};
}

void setTrace(bool trace) { g_trace = trace; }

}  // namespace local_port_relay
