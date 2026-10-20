#include "join_admission.h"

#include <mutex>

#include "guest_call.h"
#include "join_admission_rules.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kPayloadArg = 0;
constexpr size_t kOffsetArg = 1;
constexpr size_t kContextArg = 2;
constexpr size_t kContextWords = 4;
constexpr size_t kContextMemberWord = sr2::kMessageContextMemberOffset / sizeof(uint32_t);
constexpr size_t kContextAddressWord = 2;  // the dispatcher passes the member's source address block here
constexpr uintptr_t kMemberAddressBlockOffset = 0x28;

void* g_requestOriginal = nullptr;
void* g_tickOriginal = nullptr;
std::mutex g_lock;
join_admission_rules::WaitingJoiners g_waiting;
bool g_trace = false;

bool admissionBusy() {
    return join_admission_rules::busy(readDword(sr2::kAdmissionAcceptDialog), readByte(sr2::kAdmissionDeferred),
                                      readDword(sr2::kFriendsQueryCallback));
}

void sendWait(uintptr_t member) {
    const join_admission_rules::Message message = join_admission_rules::waitMessage(readByte(sr2::kSetupResponseWideFlag) != 0);
    guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(member)},
                     {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.bytes.data())),
                      static_cast<uint32_t>(message.length), 0, 0});
}

// N1a: the handler of a setup_request of type 0. The request waits (eax 1) while another joiner is being admitted.
void __cdecl admissionGate(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uintptr_t sender = readDword(args[kContextArg] + sr2::kMessageContextMemberOffset);
    if (!join_admission_rules::mustWait(readDword(sr2::kAdmissionPendingMember), sender, admissionBusy())) {
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_requestOriginal), frame->regs,
                                           {args[kPayloadArg], args[kOffsetArg], args[kContextArg]});
        return;
    }
    uint32_t& offset = *reinterpret_cast<uint32_t*>(args[kOffsetArg]);
    const uint8_t request = readByte(args[kPayloadArg] + offset);
    ++offset;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_waiting.add(sender, request);
    }
    sendWait(sender);
    if (g_trace) {
        logger::write("trace: admission: member 0x%X waits, member 0x%X is being admitted, request %u",
                      static_cast<unsigned>(sender), static_cast<unsigned>(readDword(sr2::kAdmissionPendingMember)), request);
    }
    frame->regs.eax = 1;
}

// N1b: the member left callback (ebx = the leaver). The leaver stops waiting; the open admission closes only for it.
void __cdecl clearAdmissionIfLeaver(uintptr_t leaver) {
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_waiting.drop(leaver);
    }
    const uintptr_t pending = readDword(sr2::kAdmissionPendingMember);
    if (pending == 0 || pending == leaver) guest_call::call(sr2::kClearAdmission, {});
}

// N1c: before the co-op tick, the next joiner that still is a member goes through the handler as if its request had just
// arrived (the game's own function, not the hook).
void __cdecl admissionPump(GuestFrame*) {
    join_admission_rules::WaitingJoiners::Entry entry{};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_waiting.size() == 0 || admissionBusy()) return;
        if (!g_waiting.next(remote_players::isRemoteMember, entry)) return;
    }
    uint8_t payload[1] = {entry.request};
    uint32_t offset = 0;
    uint32_t context[kContextWords] = {};
    context[kContextMemberWord] = static_cast<uint32_t>(entry.member);
    context[kContextAddressWord] = static_cast<uint32_t>(entry.member + kMemberAddressBlockOffset);
    if (g_trace) logger::write("trace: admission: member 0x%X is admitted now", static_cast<unsigned>(entry.member));
    guest_call::call(reinterpret_cast<uintptr_t>(g_requestOriginal), {},
                     {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload)),
                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&offset)),
                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context))});
}

PARTNER_WRAP_STUB(admissionGateStub, admissionGate, 0)
TRACE_ENTRY_STUB(admissionPumpStub, admissionPump, g_tickOriginal)

// Replaces `call 0x8329B0`; ebx is the leaver.
void __declspec(naked) clearAdmissionStub() {
    __asm {
        push ebx
        call clearAdmissionIfLeaver
        add esp, 4
        ret
    }
}

}  // namespace

namespace join_admission {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::AdmissionGate: return {reinterpret_cast<void*>(&admissionGateStub), &g_requestOriginal};
        case Thunk::ClearAdmissionIfLeaver: return {reinterpret_cast<void*>(&clearAdmissionStub), nullptr};
        case Thunk::AdmissionPump: return {reinterpret_cast<void*>(&admissionPumpStub), &g_tickOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace join_admission
