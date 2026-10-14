#include "join_net_trace.h"

#include <cstring>
#include <vector>

#include "code_patch.h"
#include "hooks.h"
#include "join_net_trace_sites.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using join_net_trace_sites::Kind;
using join_net_trace_sites::Site;

constexpr size_t kNameBytes = 33;
constexpr size_t kReturnDepth = 4;
constexpr uintptr_t kNatCookieOffset = 0;      // the first field of a queued negotiation
constexpr uintptr_t kRequestMemberOffset = 4;  // ctx[1]: the member the sender is mapped to
constexpr uintptr_t kDenyReasonOffset = 4;     // the reason byte inside a join_deny payload
constexpr size_t kNatResultArg = 0;
constexpr size_t kNatQueuedArg = 3;

void* g_natRequest = nullptr;
void* g_natComplete = nullptr;
void* g_joinReq = nullptr;
void* g_denySend = nullptr;
void* g_memberAdd = nullptr;
void* g_memberCreate = nullptr;
void* g_joinReqSend = nullptr;
void* g_denyReceive = nullptr;
void* g_joinAccReceive = nullptr;

const uintptr_t g_mappedContinue = join_net_trace_sites::kJoinReqMappedDrop + join_net_trace_sites::kBranchBytes;
const uintptr_t g_duplicateContinue = join_net_trace_sites::kJoinReqDuplicateDrop + join_net_trace_sites::kBranchBytes;
const uintptr_t g_dropExit = join_net_trace_sites::kJoinReqDropExit;

// The return addresses a return-traced function was entered with on this thread, with its first two arguments.
// Neither traced function calls itself; the depth only keeps a nested entry from losing its return.
class ReturnShadow {
public:
    struct Entry {
        uint32_t returnAddress;
        uint32_t args[2];
    };

    bool push(const Entry& entry) {
        if (depth_ == kReturnDepth) return false;
        entries_[depth_++] = entry;
        return true;
    }
    Entry pop() { return entries_[--depth_]; }

private:
    Entry entries_[kReturnDepth];
    size_t depth_ = 0;
};

thread_local ReturnShadow t_memberAdd;
thread_local ReturnShadow t_memberCreate;

struct SessionCounts {
    uint32_t members, reserved, max;
};

SessionCounts countsOf(uintptr_t session) {
    return {readDword(session + sr2::kSessionMemberCountOffset), readDword(session + sr2::kSessionReservedOffset),
            readDword(readDword(session + sr2::kSessionInfoOffset) + sr2::kInfoMaxPlayersOffset)};
}

// A player name from a message, printable ASCII only.
void readName(uintptr_t address, char (&out)[kNameBytes]) {
    memset(out, 0, sizeof(out));
    readMemory(address, out, sizeof(out) - 1);
    for (char& c : out) {
        if (c != 0 && (c < ' ' || c > '~')) c = '?';
    }
}

// The stack arguments of the traced function (entry order).
const uint32_t* argsOf(const GuestFrame* frame) { return frame->args; }

void exitMemberAddStub();
void exitMemberCreateStub();

bool redirectReturn(GuestFrame* frame, ReturnShadow& shadow, void (*exitStub)()) {
    const uint32_t* args = argsOf(frame);
    if (!shadow.push({frame->returnAddress, {args[0], args[1]}})) return false;
    frame->returnAddress = reinterpret_cast<uint32_t>(exitStub);
    return true;
}

// T1: the third player's NAT request reached the host's QR2 callback; whether a negotiation is already running.
void __cdecl traceNatRequest(GuestFrame* frame) {
    uint8_t busy = 0;
    readMemory(sr2::kNatBusyFlag, &busy, sizeof(busy));
    logger::write("trace: nat request, cookie %d, qr2 object 0x%X, negotiation running %u, advertised session 0x%X",
                  static_cast<int>(argsOf(frame)[0]), static_cast<unsigned>(frame->regs.ecx), busy,
                  static_cast<unsigned>(readDword(sr2::kAdvertisedSessionPointer)));
}

// T2: the host-side outcome of a negotiation (0 = ok, 1 = failed); its queued entry starts with the cookie.
void __cdecl traceNatComplete(GuestFrame* frame) {
    const uint32_t* args = argsOf(frame);
    logger::write("trace: nat negotiation complete, result %u, cookie %d", args[kNatResultArg],
                  static_cast<int>(readDword(args[kNatQueuedArg] + kNatCookieOffset)));
}

// T4: the host's join_req handler: the requested name, the sender's address words and the session counts.
void __cdecl traceJoinReq(GuestFrame* frame) {
    const uint32_t* args = argsOf(frame);
    const uintptr_t address = readDword(args[1]);
    char name[kNameBytes];
    readName(args[0], name);
    const uintptr_t session = readDword(sr2::kSessionPointer);
    const SessionCounts counts = countsOf(session);
    logger::write("trace: join_req handler, name \"%s\", context 0x%X, mapped member 0x%X, sender words 0x%08X 0x%08X, "
                  "session 0x%X members %u reserved %u max %u",
                  name, static_cast<unsigned>(args[1]), static_cast<unsigned>(readDword(args[1] + kRequestMemberOffset)),
                  readDword(address), readDword(address + 4), static_cast<unsigned>(session), counts.members,
                  counts.reserved, counts.max);
}

// T4a: eax is the context here.
void __cdecl traceMappedDrop(GuestFrame* frame) {
    logger::write("trace: join_req dropped, the sender is already mapped to member 0x%X",
                  static_cast<unsigned>(readDword(frame->regs.eax + kRequestMemberOffset)));
}

// T4b: eax is what the address search found.
void __cdecl traceDuplicateDrop(GuestFrame* frame) {
    logger::write("trace: join_req dropped, a member has the sender's address (search result 0x%X)",
                  static_cast<unsigned>(frame->regs.eax));
}

// T5: a join_deny is sent.
void __cdecl traceDenySend(GuestFrame* frame) {
    logger::write("trace: join_deny sent, reason %u", argsOf(frame)[0] & 0xFF);
}

// T6: the member add, stdcall(session, member, flags); its result is logged on return.
void __cdecl enterMemberAdd(GuestFrame* frame) { redirectReturn(frame, t_memberAdd, &exitMemberAddStub); }

uint32_t __cdecl leaveMemberAdd(uint32_t result) {
    const ReturnShadow::Entry entry = t_memberAdd.pop();
    const uintptr_t session = entry.args[0];
    const SessionCounts counts = countsOf(session);
    logger::write("trace: member add returned %u, session 0x%X member 0x%X, busy state %u/%u/%u, join type %u, members %u "
                  "reserved %u max %u",
                  result, static_cast<unsigned>(session), static_cast<unsigned>(entry.args[1]),
                  readDword(session + sr2::kSessionBusyStateOffset), readDword(session + sr2::kSessionBusySubstateAOffset),
                  readDword(session + sr2::kSessionBusySubstateBOffset),
                  readDword(readDword(session + sr2::kSessionInfoOffset) + sr2::kInfoJoinTypeOffset), counts.members,
                  counts.reserved, counts.max);
    return entry.returnAddress;
}

// T7: the member allocation; 0 = the pool is exhausted.
void __cdecl enterMemberCreate(GuestFrame* frame) { redirectReturn(frame, t_memberCreate, &exitMemberCreateStub); }

uint32_t __cdecl leaveMemberCreate(uint32_t result) {
    logger::write("trace: member allocated 0x%X", static_cast<unsigned>(result));
    return t_memberCreate.pop().returnAddress;
}

// T8 to T10 run on the joining machine.
void __cdecl traceJoinReqSend(GuestFrame*) { logger::write("trace: join_req sent"); }

void __cdecl traceDenyReceive(GuestFrame* frame) {
    uint8_t reason = 0;
    readMemory(argsOf(frame)[0] + kDenyReasonOffset, &reason, sizeof(reason));
    logger::write("trace: join_deny received, reason %d", static_cast<int8_t>(reason));
}

void __cdecl traceJoinAccReceive(GuestFrame*) { logger::write("trace: join_acc received"); }

TRACE_ENTRY_STUB(natRequestStub, traceNatRequest, g_natRequest)
TRACE_ENTRY_STUB(natCompleteStub, traceNatComplete, g_natComplete)
TRACE_ENTRY_STUB(joinReqStub, traceJoinReq, g_joinReq)
TRACE_ENTRY_STUB(denySendStub, traceDenySend, g_denySend)
TRACE_RETURN_STUBS(memberAddStub, exitMemberAddStub, enterMemberAdd, leaveMemberAdd, g_memberAdd)
TRACE_RETURN_STUBS(memberCreateStub, exitMemberCreateStub, enterMemberCreate, leaveMemberCreate, g_memberCreate)
TRACE_ENTRY_STUB(joinReqSendStub, traceJoinReqSend, g_joinReqSend)
TRACE_ENTRY_STUB(denyReceiveStub, traceDenyReceive, g_denyReceive)
TRACE_ENTRY_STUB(joinAccReceiveStub, traceJoinAccReceive, g_joinAccReceive)

// The drop branches: the flags are the game's own `jne`. A drop is logged, anything else carries on.
void __declspec(naked) mappedDropStub() {
    __asm {
        jne dropped
        jmp dword ptr [g_mappedContinue]
    dropped:
        pushad
        pushfd
        lea eax, [esp + 4]
        push eax
        call traceMappedDrop
        add esp, 4
        popfd
        popad
        jmp dword ptr [g_dropExit]
    }
}

void __declspec(naked) duplicateDropStub() {
    __asm {
        jne dropped
        jmp dword ptr [g_duplicateContinue]
    dropped:
        pushad
        pushfd
        lea eax, [esp + 4]
        push eax
        call traceDuplicateDrop
        add esp, 4
        popfd
        popad
        jmp dword ptr [g_dropExit]
    }
}

struct Binding {
    const char* id;
    void (*thunk)();
    void** original;  // the trampoline slot of an entry hook
};

const Binding kBindings[] = {
    {"T1", natRequestStub, &g_natRequest},     {"T2", natCompleteStub, &g_natComplete},
    {"T4", joinReqStub, &g_joinReq},
    {"T4a", mappedDropStub, nullptr},          {"T4b", duplicateDropStub, nullptr},
    {"T5", denySendStub, &g_denySend},         {"T6", memberAddStub, &g_memberAdd},
    {"T7", memberCreateStub, &g_memberCreate}, {"T8", joinReqSendStub, &g_joinReqSend},
    {"T9", denyReceiveStub, &g_denyReceive},   {"T10", joinAccReceiveStub, &g_joinAccReceive},
};

const Binding* bindingOf(const Site& site) {
    for (const Binding& binding : kBindings) {
        if (strcmp(binding.id, site.id) == 0) return &binding;
    }
    return nullptr;
}

}  // namespace

namespace join_net_trace {

bool install() {
    std::vector<CodePatch> patches;
    for (const Site& site : join_net_trace_sites::kSites) {
        const Binding* binding = bindingOf(site);
        if (!binding) {
            logger::write("join trace: no thunk for %s, nothing installed", site.id);
            return false;
        }
        patches.push_back(join_net_trace_sites::patchFor(site, reinterpret_cast<uintptr_t>(binding->thunk)));
    }
    if (!applyCodePatches(patches)) {
        logger::write("join trace: sites differ, nothing installed");
        return false;
    }
    for (const Site& site : join_net_trace_sites::kSites) {
        const Binding* binding = bindingOf(site);
        if (site.kind == Kind::Entry) {
            hooks::install(site.id, site.address, reinterpret_cast<void*>(binding->thunk), binding->original);
        }
    }
    logger::write("join trace: %zu sites installed", join_net_trace_sites::kSiteCount);
    return true;
}

}  // namespace join_net_trace
