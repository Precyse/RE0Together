// Thunks of the activities and diversions themselves: Insurance Fraud, Demolition Derby, Zombie Uprising and the two
// competitive diversions.
#include <atomic>

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
constexpr uint8_t kZombieAnswerAccept = 3;

void* g_derbyOriginal = nullptr;
void* g_zombieAnswerOriginal = nullptr;
void* g_catAndMouseOriginal = nullptr;
void* g_deathTagOriginal = nullptr;

// The member that asked for the zombie activity, until the start takes it as the partner.
std::atomic<uintptr_t> g_zombieRequester{0};
activity_rules::DeclineTally g_zombieTally;

uint8_t byteAt(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// AV1: any remote player human still in a ragdoll keeps the Insurance Fraud level going.
uint32_t __cdecl anyRemoteRagdolling() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        if (byteAt(snapshot.humans[i].human + sr2::kHumanRagdollOffset) != 0) return 1;
    }
    return 0;
}

// The test that follows reads eax; ecx and edx are live in the code after it.
void __declspec(naked) anyRemoteRagdollingStub() {
    __asm {
        push ecx
        push edx
        call anyRemoteRagdolling
        pop edx
        pop ecx
        ret
    }
}

// AV16: a spectator ignores the garage, car spawn and arena warp of a derby it does not drive in.
void __cdecl derbyParticipantsOnly(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uint32_t local = readDword(readDword(sr2::kLocalPlayer) + sr2::kObjectHandleOffset);
    const uint8_t sub = byteAt(args[kBufferArg]);
    if (activity_rules::derbyFlowIgnored(local, readDword(sr2::kMissionOwnerHandle), readDword(sr2::kMissionPartnerHandle), sub)) {
        if (activity_state::tracing()) logger::write("trace: derby sub %u ignored, this player is a spectator", sub);
        return;
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_derbyOriginal), frame->regs,
                                       {args[kBufferArg], args[kContextArg]});
}

// AC12: the member that asks for the zombie activity is remembered (the host sees it under the sender scope).
uintptr_t __cdecl recordZombieRequester() {
    const uintptr_t member = partner_scope::partnerMember();
    if (activity_state::isHost()) g_zombieRequester = member;
    return member;
}

// AC13: the zombie partner is the member that asked, else the getter's answer (the accepting guest under the scope).
uintptr_t __cdecl zombiePartner() {
    const uintptr_t requester = g_zombieRequester.exchange(0);
    if (requester && !partner_scope::overrideActive()) {
        const uintptr_t human = remote_players::humanOfMember(requester);
        if (human) return human;
    }
    return partner_scope::partnerHuman();
}

// AC14: a decline or busy answer to the zombie request ends it only once every remote member has answered so.
void __cdecl zombieAnswerGate(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uint32_t dialog = readDword(sr2::kZombieDialog);
    if (activity_state::isHost() && dialog && byteAt(args[kBufferArg]) != kZombieAnswerAccept) {
        uintptr_t required[activity_rules::kMaxMembers];
        const size_t count = activity_state::remoteMembersInGame(required);
        const uintptr_t member = activity_state::senderOf(args[kContextArg]);
        const bool all = g_zombieTally.declinedAll(dialog, member, required, count);
        if (activity_state::tracing()) {
            logger::write("trace: zombie request declined by member 0x%X, %s", static_cast<unsigned>(member),
                          all ? "every remote has declined" : "waiting for the others");
        }
        if (!all) return;
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_zombieAnswerOriginal), frame->regs,
                                       {args[kBufferArg], args[kContextArg]});
}

// AD3, AD4: cat and mouse and death tag are 1 against 1; with more remote players they do not start.
void blockOrStart(GuestFrame* frame, void* original, const char* name) {
    if (activity_rules::blocksCompetitiveStart(remote_players::remoteHumanCount())) {
        if (activity_state::tracing()) logger::write("trace: %s does not start with more than one remote player", name);
        frame->regs.eax = 0;
        return;
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(original), frame->regs);
}

void __cdecl blockCatAndMouse(GuestFrame* frame) { blockOrStart(frame, g_catAndMouseOriginal, "cat and mouse"); }

void __cdecl blockDeathTag(GuestFrame* frame) { blockOrStart(frame, g_deathTagOriginal, "death tag"); }

PARTNER_WRAP_STUB(derbyStub, derbyParticipantsOnly, 0)
PARTNER_WRAP_STUB(zombieAnswerStub, zombieAnswerGate, 0)
PARTNER_WRAP_STUB(catAndMouseStub, blockCatAndMouse, 0)
PARTNER_WRAP_STUB(deathTagStub, blockDeathTag, 0)

}  // namespace

namespace activity_thunks {

partner_thunks::Binding bindGames(Thunk thunk) {
    switch (thunk) {
        case Thunk::AnyRemoteRagdolling: return {reinterpret_cast<void*>(&anyRemoteRagdollingStub), nullptr};
        case Thunk::DerbyParticipantsOnly: return {reinterpret_cast<void*>(&derbyStub), &g_derbyOriginal};
        case Thunk::RecordZombieRequester: return {reinterpret_cast<void*>(&recordZombieRequester), nullptr};
        case Thunk::ZombiePartner: return {reinterpret_cast<void*>(&zombiePartner), nullptr};
        case Thunk::ZombieAnswerGate: return {reinterpret_cast<void*>(&zombieAnswerStub), &g_zombieAnswerOriginal};
        case Thunk::BlockCatAndMouse: return {reinterpret_cast<void*>(&catAndMouseStub), &g_catAndMouseOriginal};
        case Thunk::BlockDeathTag: return {reinterpret_cast<void*>(&deathTagStub), &g_deathTagOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace activity_thunks
