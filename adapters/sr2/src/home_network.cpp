#include "home_network.h"

#include "home_network_rules.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

bool g_trace = false;
const uintptr_t g_findDuplicateMember = sr2::kFindDuplicateMember;

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

}  // namespace

namespace home_network {

void setTrace(bool trace) { g_trace = trace; }

uintptr_t duplicateJoinSenderThunk() { return reinterpret_cast<uintptr_t>(&duplicateJoinSenderStub); }

}  // namespace home_network
