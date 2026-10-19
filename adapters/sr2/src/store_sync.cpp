#include "store_sync.h"

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "player_visuals_rules.h"
#include "remote_players.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "store_rules.h"
#include "store_sites.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
using store_rules::Route;

constexpr size_t kMaxRemotes = sr2::kMaxWalkedPlayers;
constexpr size_t kBufferArg = 0;  // the broadcast is cdecl(buffer, length)
constexpr size_t kLengthArg = 1;
constexpr uint32_t kNoCallback = 0;
constexpr uint32_t kNoContext = 0;
constexpr int kBoxArguments = 2;
constexpr uint32_t kResultByte = 0xFF;  // the point test answers in al

bool g_trace = false;
const uintptr_t g_enterResume = sr2::kStoreEnterHideResume;
const uintptr_t g_exitResume = sr2::kStoreExitShowResume;
const uintptr_t g_garageResume = sr2::kGarageHideResume;

// What the game's block did to the default partner, for every remote player human. `what` starts the trace line.
void setRemotesHidden(bool hide, const char* what) {
    uintptr_t humans[kMaxRemotes];
    const size_t count = remote_players::remoteHumans(humans, kMaxRemotes);
    for (size_t i = 0; i < count; ++i) {
        uint32_t& flags = dwordField(humans[i], sr2::kHumanVisibilityFlagsOffset);
        flags = store_rules::withHidden(flags, hide);
    }
    if (g_trace && count > 0) logger::write("trace: store: %s remote humans %s", what, player_visuals_rules::humansText(humans, count).c_str());
}

void __cdecl hideRemotesEnteringStore() { setRemotesHidden(true, "entered, hid"); }
void __cdecl showRemotesLeavingStore() { setRemotesHidden(false, "left, showed"); }
void __cdecl hideRemotesInGarage() { setRemotesHidden(true, "garage, hid"); }

// The three blocks are jumped out of in the middle of a function and continue after the block.
PARTNER_RESUME_STUB(hideRemotesInStoreStub, hideRemotesEnteringStore, g_enterResume)
PARTNER_RESUME_STUB(showRemotesAfterStoreStub, showRemotesLeavingStore, g_exitResume)
PARTNER_RESUME_STUB(hideRemotesInGarageStub, hideRemotesInGarage, g_garageResume)

// The game's point in box test (ecx = point, eax = box centre, esi = orientation, two stack arguments) for `point`; its eax.
uint32_t pointInBox(const GuestFrame* frame, uintptr_t point) {
    guest_call::Regs regs = frame->regs;
    regs.ecx = static_cast<uint32_t>(point);
    guest_call::callWithRegs(sr2::kPointInBox, &regs, frame->args, kBoxArguments);
    return regs.eax;
}

// GB1: the garage trigger's test of the vehicle box, for every remote player human; the answer is the first that is inside. Without a
// remote human it is the game's call as it was.
void __cdecl anyRemoteInVehicleBox(GuestFrame* frame) {
    uintptr_t humans[kMaxRemotes];
    const size_t count = remote_players::remoteHumans(humans, kMaxRemotes);
    if (count == 0) {
        frame->regs.eax = pointInBox(frame, frame->regs.ecx);
        return;
    }
    uint32_t answer = 0;
    for (size_t i = 0; i < count; ++i) {
        answer = pointInBox(frame, humans[i] + sr2::kObjectPositionOffset);
        if (answer & kResultByte) {
            if (g_trace) logger::write("trace: store: garage store refused, remote human 0x%X in the vehicle box", static_cast<unsigned>(humans[i]));
            break;
        }
    }
    frame->regs.eax = answer;
}

// The game's broadcast of a built message (edi = the session, cdecl(buffer, length)) to every member but this one, or to `member` alone.
void send(const Route route, uintptr_t member, GuestFrame* frame) {
    const uint32_t buffer = frame->args[kBufferArg];
    const uint32_t length = frame->args[kLengthArg];
    if (route == Route::Member) {
        frame->regs.eax = guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(member)}, {buffer, length, kNoCallback, kNoContext});
    } else {
        frame->regs.eax = guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {buffer, length});
    }
}

// LM1: the human the owner's compare is given in place of get_remote_player. Any remote owner is "the partner"; anyone else (the local
// player, nobody) leaves the game's default partner, which is not the owner, so the menu opens here.
uintptr_t __cdecl activityOwnerIfRemote() {
    const uintptr_t owner = guest_call::call(sr2::kActivityOwnerHuman, {});
    if (g_trace && !remote_players::isRemotePlayerHuman(owner)) {
        logger::write("trace: activity level: owner 0x%X not remote, menu here", static_cast<unsigned>(owner));
    }
    return partner_identity::match(owner);
}

// LM2: the menu request (act 0x0B), sent to the member that owns the activity only.
void __cdecl sendToActivityOwner(GuestFrame* frame) {
    const uintptr_t owner = guest_call::call(sr2::kActivityOwnerHuman, {});
    const uintptr_t member = remote_players::memberOfHuman(owner);
    const Route route = store_rules::levelMenuRoute(member);
    if (g_trace && route == Route::Member) {
        logger::write("trace: activity level: owner human 0x%X remote, menu request to member 0x%X slot %u", static_cast<unsigned>(owner),
                      static_cast<unsigned>(member), readByte(member + sr2::kMemberSlotOffset));
    }
    send(route, member, frame);
}

// LM3, LM4: a guest's choice or cancel, sent to the host member only.
void __cdecl sendAnswerToHost(GuestFrame* frame) {
    const uintptr_t session = readDword(sr2::kSessionPointer);
    const bool isHost = session != 0 && readByte(session + sr2::kSessionHostFlagOffset) != 0;
    const uintptr_t hostMember = session != 0 ? readDword(session + sr2::kSessionHostMemberOffset) : 0;
    const Route route = store_rules::answerRoute(isHost, hostMember);
    if (g_trace && route == Route::Member) {
        logger::write("trace: activity level: answer %s to the host member 0x%X", frame->returnAddress == sr2::kLevelChoiceSendReturn ? "choice" : "cancel",
                      static_cast<unsigned>(hostMember));
    }
    send(route, hostMember, frame);
}

PARTNER_WRAP_STUB(anyRemoteInVehicleBoxStub, anyRemoteInVehicleBox, 0)
PARTNER_WRAP_STUB(sendToActivityOwnerStub, sendToActivityOwner, 0)
PARTNER_WRAP_STUB(sendAnswerToHostStub, sendAnswerToHost, 0)

}  // namespace

namespace store_sync {

partner_thunks::Binding bindingOf(Thunk thunk) {
    switch (thunk) {
        case Thunk::HideRemotesInStore: return {reinterpret_cast<void*>(&hideRemotesInStoreStub), nullptr};
        case Thunk::ShowRemotesAfterStore: return {reinterpret_cast<void*>(&showRemotesAfterStoreStub), nullptr};
        case Thunk::HideRemotesInGarage: return {reinterpret_cast<void*>(&hideRemotesInGarageStub), nullptr};
        case Thunk::AnyRemoteInVehicleBox: return {reinterpret_cast<void*>(&anyRemoteInVehicleBoxStub), nullptr};
        case Thunk::ActivityOwnerIfRemote: return {reinterpret_cast<void*>(&activityOwnerIfRemote), nullptr};
        case Thunk::SendToActivityOwner: return {reinterpret_cast<void*>(&sendToActivityOwnerStub), nullptr};
        case Thunk::SendAnswerToHost: return {reinterpret_cast<void*>(&sendAnswerToHostStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

void install(bool trace) {
    g_trace = trace;
    for (const store_sites::Set& set : store_sites::kSets) {
        if (!site_installer::sitesMatch(set.sites)) {
            logger::write("store: %s not patched, the bytes differ", set.name);
            continue;
        }
        const site_installer::Result result = site_installer::install(set.sites, false, &bindingOf);
        if (result.installed) {
            logger::write("store: %s, %zu sites patched", set.name, result.patched);
        } else {
            logger::write("store: %s not patched", set.name);
        }
    }
}

}  // namespace store_sync
