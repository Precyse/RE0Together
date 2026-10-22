// Thunks of the activities and diversions themselves: Insurance Fraud, Trail Blazing, Demolition Derby, Zombie Uprising,
// Chop Shop and the two competitive diversions.
#include <atomic>

#include "activity_declines.h"
#include "activity_rules.h"
#include "activity_state.h"
#include "activity_thunks.h"
#include "diversion_rules.h"
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
void* g_catAndMouseOriginal = nullptr;
void* g_deathTagOriginal = nullptr;

// The member that asked for the zombie activity, until the start takes it as the partner.
std::atomic<uintptr_t> g_zombieRequester{0};

enum CompetitiveDiversion : size_t { kCatAndMouse, kDeathTag, kCompetitiveDiversions };
diversion_rules::EdgeLog<kCompetitiveDiversions> g_competitiveLog;

// AV1: any remote player human still in a ragdoll keeps the Insurance Fraud level going.
uint32_t __cdecl anyRemoteRagdolling() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        if (readByte(snapshot.humans[i].human + sr2::kHumanRagdollOffset) != 0) return 1;
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

// AV2, AV3 (freeze), AV12-AV15 (Trail Blazing flags): the game function runs for every remote player human, not the
// partner only. The functions are stdcall(human, flag) and send to the human's owner themselves.
void forEachRemote(uintptr_t function, uint32_t flag) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        guest_call::call(function, {}, {static_cast<uint32_t>(snapshot.humans[i].human), flag});
    }
}

void __stdcall freezeEachRemote(uint32_t, uint32_t flag) { forEachRemote(sr2::kFreezeHuman, flag); }

void __stdcall flagAEachRemote(uint32_t, uint32_t flag) { forEachRemote(sr2::kHumanFlagA, flag); }

void __stdcall flagBEachRemote(uint32_t, uint32_t flag) { forEachRemote(sr2::kHumanFlagB, flag); }

// AV16: a spectator ignores the garage, car spawn and arena warp of a derby it does not drive in.
void __cdecl derbyParticipantsOnly(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uint32_t local = readDword(readDword(sr2::kLocalPlayer) + sr2::kObjectHandleOffset);
    const uint8_t sub = readByte(args[kBufferArg]);
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
    if (activity_state::isHost() && dialog && readByte(args[kBufferArg]) != kZombieAnswerAccept) {
        if (!activity_declines::zombieDeclined(dialog, activity_state::senderOf(args[kContextArg]))) return;
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(*activity_declines::zombieAnswerOriginal()), frame->regs,
                                       {args[kBufferArg], args[kContextArg]});
}

// AD3, AD4: cat and mouse and death tag are 1 against 1; with more remote players they do not start. The game asks every
// frame, so the trace says it when the answer of a diversion changes.
void blockOrStart(GuestFrame* frame, void* original, CompetitiveDiversion which, const char* name) {
    const bool blocked = activity_rules::blocksCompetitiveStart(remote_players::remoteHumanCount());
    if (activity_state::tracing() && g_competitiveLog.changed(which, blocked)) {
        if (blocked) {
            logger::write("trace: %s does not start with more than one remote player", name);
        } else {
            logger::write("trace: %s may start", name);
        }
    }
    if (blocked) {
        frame->regs.eax = 0;
        return;
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(original), frame->regs);
}

void __cdecl blockCatAndMouse(GuestFrame* frame) { blockOrStart(frame, g_catAndMouseOriginal, kCatAndMouse, "cat and mouse"); }

void __cdecl blockDeathTag(GuestFrame* frame) { blockOrStart(frame, g_deathTagOriginal, kDeathTag, "death tag"); }

// AD5: Chop Shop delivery: the remote riding in the delivered car, else the default partner. ebx is the delivery; the
// car is the object its vehicle slot holds.
uintptr_t __cdecl remoteInDeliveredCar(uintptr_t delivery) {
    const uint32_t car = readDword(readDword(delivery + sr2::kDeliveryVehicleOffset) + sr2::kObjectHandleOffset);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        const uintptr_t human = snapshot.humans[i].human;
        if (readDword(human + sr2::kHumanVehicleOffset) == car) return human;
    }
    return partner_scope::partnerHuman();
}

PARTNER_REG_STUB(remoteInDeliveredCarStub, ebx, remoteInDeliveredCar)

// AD6 (probe H): the Chop Shop credit goes to the remotes riding in the delivered car instead of to everyone. The car is
// the only argument of the sender, [esp + 0x538] where it was entered.
void __cdecl sendToRidersOfDeliveredCar(GuestFrame* frame) {
    const uint32_t car = readDword(readDword(reinterpret_cast<uintptr_t>(&frame->returnAddress) + sr2::kChopCreditCarArg) +
                                   sr2::kObjectHandleOffset);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human || !remote.member || readDword(remote.human + sr2::kHumanVehicleOffset) != car) continue;
        guest_call::call(sr2::kSendToMember, {.esi = remote.member}, {frame->args[0], frame->args[1], 0, 0});
    }
}

PARTNER_WRAP_STUB(chopCreditStub, sendToRidersOfDeliveredCar, 0)
PARTNER_WRAP_STUB(derbyStub, derbyParticipantsOnly, 0)
PARTNER_WRAP_STUB(zombieAnswerStub, zombieAnswerGate, 0)
PARTNER_WRAP_STUB(catAndMouseStub, blockCatAndMouse, 0)
PARTNER_WRAP_STUB(deathTagStub, blockDeathTag, 0)

}  // namespace

namespace activity_thunks {

partner_thunks::Binding bindGames(Thunk thunk) {
    switch (thunk) {
        case Thunk::EachRemote965F40: return {reinterpret_cast<void*>(&freezeEachRemote), nullptr};
        case Thunk::EachRemote968010: return {reinterpret_cast<void*>(&flagAEachRemote), nullptr};
        case Thunk::EachRemote9682A0: return {reinterpret_cast<void*>(&flagBEachRemote), nullptr};
        case Thunk::RemoteInVehicleEbx1c: return {reinterpret_cast<void*>(&remoteInDeliveredCarStub), nullptr};
        case Thunk::SendToRemotesInVehicleEsp538: return {reinterpret_cast<void*>(&chopCreditStub), nullptr};
        case Thunk::AnyRemoteRagdolling: return {reinterpret_cast<void*>(&anyRemoteRagdollingStub), nullptr};
        case Thunk::DerbyParticipantsOnly: return {reinterpret_cast<void*>(&derbyStub), &g_derbyOriginal};
        case Thunk::RecordZombieRequester: return {reinterpret_cast<void*>(&recordZombieRequester), nullptr};
        case Thunk::ZombiePartner: return {reinterpret_cast<void*>(&zombiePartner), nullptr};
        case Thunk::ZombieAnswerGate: return {reinterpret_cast<void*>(&zombieAnswerStub), activity_declines::zombieAnswerOriginal()};
        case Thunk::BlockCatAndMouse: return {reinterpret_cast<void*>(&catAndMouseStub), &g_catAndMouseOriginal};
        case Thunk::BlockDeathTag: return {reinterpret_cast<void*>(&deathTagStub), &g_deathTagOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace activity_thunks
