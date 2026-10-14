// Thunks of the activity framework: the sender scope of the message handlers, the start request handshake, the cleanup
// message and the start position.
#include "activity_rules.h"
#include "activity_state.h"
#include "activity_thunks.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kBufferArg = 0;
constexpr size_t kContextArg = 1;

void* g_actPacketOriginal = nullptr;
void* g_miscPacketOriginal = nullptr;
void* g_missionRequestOriginal = nullptr;
void* g_gameplayChangedOriginal = nullptr;
void* g_raceRequestOriginal = nullptr;

const uintptr_t g_declineHandler = sr2::kDeclineHandler;

activity_rules::DeclineTally g_declineTally;

uint8_t byteAt(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// SENDER_SCOPE: the handler runs with every getter of "the partner" answering the member the message came from, which
// is what the 2-player code means by "the partner" there. Returns the handler's result.
uint32_t runScoped(GuestFrame* frame, void* original, const char* hook) {
    const uint32_t* args = frame->args;
    const uintptr_t member = activity_state::senderOf(args[kContextArg]);
    const uintptr_t human = remote_players::isRemoteMember(member) ? remote_players::humanOfMember(member) : 0;
    if (activity_state::tracing()) {
        logger::writeUnlessRepeated("trace: scope %s sub %u sender member 0x%X human 0x%X%s", hook, byteAt(args[kBufferArg]),
                                    static_cast<unsigned>(member), static_cast<unsigned>(human), human ? "" : " (no scope)");
    }
    const uintptr_t target = reinterpret_cast<uintptr_t>(original);
    if (!human) return guest_call::call(target, frame->regs, {args[kBufferArg], args[kContextArg]});
    partner_scope::Override scope({human, member});
    return guest_call::call(target, frame->regs, {args[kBufferArg], args[kContextArg]});
}

void __cdecl scopeActPacket(GuestFrame* frame) { frame->regs.eax = runScoped(frame, g_actPacketOriginal, "act"); }

void __cdecl scopeMiscPacket(GuestFrame* frame) { frame->regs.eax = runScoped(frame, g_miscPacketOriginal, "misc"); }

void __cdecl scopeMissionRequest(GuestFrame* frame) {
    frame->regs.eax = runScoped(frame, g_missionRequestOriginal, "mssn_req");
}

void __cdecl scopeGameplayChanged(GuestFrame* frame) {
    frame->regs.eax = runScoped(frame, g_gameplayChangedOriginal, "gameplay_changed");
}

// The inviting member is remembered for the answer, which is sent from a dialog.
void __cdecl scopeRaceRequest(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    if (byteAt(args[kBufferArg]) == sr2::kRaceSubInvite) {
        const uintptr_t member = activity_state::senderOf(args[kContextArg]);
        if (remote_players::isRemoteMember(member)) activity_state::setRacePeer(member);
    }
    frame->regs.eax = runScoped(frame, g_raceRequestOriginal, "race_req");
}

// AF6: a decline, busy or low respect reply cancels the start for everyone only once every remote member in the game
// has declined. Nonzero: run the game's decline handler.
uint32_t __cdecl declineEnds(uintptr_t context) {
    const uintptr_t member = activity_state::senderOf(context);
    if (!member) return 1;
    uintptr_t required[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    const uint32_t pending = readDword(sr2::kPendingMissionId);
    const bool all = g_declineTally.declinedAll(pending, member, required, count);
    if (activity_state::tracing()) {
        logger::write("trace: start declined by member 0x%X for request %u, %s", static_cast<unsigned>(member), pending,
                      all ? "every remote has declined, the start ends" : "the start goes on");
    }
    return all ? 1 : 0;
}

// The host's decline branch called the handler with edx = the buffer and (cursor, context) on the stack, which the
// caller pops. The handler is entered with all of that untouched, or the call is skipped.
void __declspec(naked) declineStub() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 0x10]
        call declineEnds
        add esp, 4
        pop edx
        pop ecx
        test eax, eax
        jz skipped
        jmp dword ptr [g_declineHandler]
    skipped:
        ret
    }
}

// AF7: the cleanup message goes to every remote member (esi is the member for the game's send).
void __cdecl sendToEachRemote(GuestFrame* frame) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const uint32_t* args = frame->args;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t member = snapshot.pairing.items[i].member;
        if (member) guest_call::call(sr2::kSendToMember, {.esi = member}, {args[0], args[1], args[2], args[3]});
    }
}

// AF8: the start position warp covers every remote when everyone takes part in the activity, else the partner only.
void __cdecl teleportForActivity(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    auto warp = [&](uintptr_t human) {
        guest_call::call(sr2::kTeleportHuman, {.ecx = human}, {args[0], args[1], args[2], args[3], args[4], args[5]});
    };
    const int32_t type = activity_state::runningActivityType();
    if (!activity_rules::isAllPlayersType(type)) {
        warp(frame->regs.ecx);
        return;
    }
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (!human || (guest_call::call(sr2::kHumanIsBusy, {}, {static_cast<uint32_t>(human)}) & 0xFF) != 0) continue;
        warp(human);
    }
    if (activity_state::tracing()) logger::write("trace: start warp of activity type %d for every remote", type);
}

// AD2: a denial answers the requesting member only; outside a handler it is the game's broadcast.
void __cdecl sendDenial(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    if (partner_scope::overrideActive()) {
        guest_call::call(sr2::kSendToMember, {.esi = partner_scope::partnerMember()}, {args[0], args[1], 0, 0});
    } else {
        guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {args[0], args[1]});
    }
}

PARTNER_WRAP_STUB(scopeActPacketStub, scopeActPacket, 0)
PARTNER_WRAP_STUB(scopeMiscPacketStub, scopeMiscPacket, 0)
PARTNER_WRAP_STUB(scopeMissionRequestStub, scopeMissionRequest, 0)
PARTNER_WRAP_STUB(scopeGameplayChangedStub, scopeGameplayChanged, 0)
PARTNER_WRAP_STUB(scopeRaceRequestStub, scopeRaceRequest, 0)
PARTNER_WRAP_STUB(sendToEachRemoteStub, sendToEachRemote, 0)
PARTNER_WRAP_STUB(teleportForActivityStub, teleportForActivity, 0)
PARTNER_WRAP_STUB(sendDenialStub, sendDenial, 0)

}  // namespace

namespace activity_thunks {

partner_thunks::Binding bindFramework(Thunk thunk) {
    switch (thunk) {
        case Thunk::ScopeActPacket: return {reinterpret_cast<void*>(&scopeActPacketStub), &g_actPacketOriginal};
        case Thunk::ScopeMiscPacket: return {reinterpret_cast<void*>(&scopeMiscPacketStub), &g_miscPacketOriginal};
        case Thunk::ScopeMissionRequest:
            return {reinterpret_cast<void*>(&scopeMissionRequestStub), &g_missionRequestOriginal};
        case Thunk::ScopeGameplayChanged:
            return {reinterpret_cast<void*>(&scopeGameplayChangedStub), &g_gameplayChangedOriginal};
        case Thunk::ScopeRaceRequest: return {reinterpret_cast<void*>(&scopeRaceRequestStub), &g_raceRequestOriginal};
        case Thunk::DeclineUnlessAllDeclined: return {reinterpret_cast<void*>(&declineStub), nullptr};
        case Thunk::SendToEachRemoteMember: return {reinterpret_cast<void*>(&sendToEachRemoteStub), nullptr};
        case Thunk::TeleportRemotesForActivity: return {reinterpret_cast<void*>(&teleportForActivityStub), nullptr};
        case Thunk::SendToScopedMember: return {reinterpret_cast<void*>(&sendDenialStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace activity_thunks
