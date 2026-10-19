// Selection thunks of Crowd Control: a call of the partner getter becomes the remote that the test around it is about (the one that
// holds the celebrity, the one nearest to the object or the remote start), and the warp of the level start and end reaches every
// remote. One remote gives the shipped answer.
#include "activity_state.h"
#include "crowd_control_remotes.h"
#include "crowd_control_rules.h"
#include "crowd_control_thunks.h"
#include "diversion_trace.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr unsigned kKeyShift = 32;

rate_limit::OncePerKey g_holderSeen;
rate_limit::OncePerKey g_objectSeen;
rate_limit::OncePerKey g_startWaitSeen;

// One trace key for a pair of values the line names.
uint64_t pairKey(uintptr_t first, uintptr_t second) { return (static_cast<uint64_t>(first) << kKeyShift) | second; }

// CR1 to CR6: the remote that holds the celebrity in edi (its handle is the one at +0x44 of the celebrity), else the default
// partner. At most one human holds the celebrity, so every call of a frame gets the same answer.
uintptr_t __cdecl remoteHoldingCelebrity(uintptr_t celebrity) {
    if (partner_scope::overrideActive()) return partner_scope::partnerHuman();
    const uint32_t handle = readDword(celebrity + sr2::kObjectHandleOffset);
    const crowd_control_remotes::List remotes = crowd_control_remotes::take();
    uint32_t held[partner_rules::kMaxWalked];
    for (size_t i = 0; i < remotes.count; ++i) held[i] = readDword(remotes.humans[i] + sr2::kHumanHeldOffset);
    const size_t at = crowd_control_rules::holderIndex(held, remotes.count, handle);
    if (at == crowd_control_rules::kNone) return partner_scope::partnerHuman();
    if (diversion_trace::firstTime(g_holderSeen, pairKey(handle, remotes.humans[at]))) {
        logger::write("trace: crowd: celeb 0x%X held by remote 0x%X", handle, static_cast<unsigned>(remotes.humans[at]));
    }
    return remotes.humans[at];
}

// The remote nearest to `point`, else the default partner (no remote).
uintptr_t nearestRemoteOrPartner(uint32_t point) {
    const uintptr_t nearest = partner_identity::nearest(point);
    return nearest ? nearest : partner_scope::partnerHuman();
}

// CR7: the object in esi waits while the local player and every remote are farther than 7 m, so the nearest remote decides.
uintptr_t __cdecl nearestRemoteToCelebCar(uintptr_t object) {
    if (partner_scope::overrideActive() || !object) return partner_scope::partnerHuman();
    const uintptr_t nearest = nearestRemoteOrPartner(static_cast<uint32_t>(object + sr2::kObjectPositionOffset));
    if (diversion_trace::firstTime(g_objectSeen, pairKey(object, nearest))) {
        logger::write("trace: crowd: object 0x%X nearest remote 0x%X", static_cast<unsigned>(object), static_cast<unsigned>(nearest));
    }
    return nearest;
}

// CR8: the start wait holds while a remote stands at the remote start nav of the running event (esi is the level).
uintptr_t __cdecl nearestRemoteToRemoteStart(uintptr_t level) {
    if (partner_scope::overrideActive() || !level) return partner_scope::partnerHuman();
    const uintptr_t events = readDword(level + sr2::kLevelInstanceOffset);
    const uint32_t event = readDword(level + sr2::kCrowdEventIndexOffset);
    const uintptr_t nav = events ? readDword(events + crowd_control_rules::navOffset(event, crowd_control_rules::NavSide::Remote)) : 0;
    const uintptr_t answer = nav ? nearestRemoteOrPartner(static_cast<uint32_t>(nav + sr2::kNavPositionOffset)) : partner_scope::partnerHuman();
    if (diversion_trace::firstTime(g_startWaitSeen, answer)) {
        logger::write("trace: crowd: start wait, nearest remote 0x%X to the remote start", static_cast<unsigned>(answer));
    }
    return answer;
}

// CR13, CR16: the warp of a remote player human reaches every remote. The local player, no human and any other human keep the
// game's call (thiscall, ecx = human, six stack arguments the caller pops).
void __cdecl teleportLocalOrEachRemote(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    auto warp = [&](uintptr_t human) {
        return guest_call::call(sr2::kTeleportHuman, {.ecx = static_cast<uint32_t>(human)}, {args[0], args[1], args[2], args[3], args[4], args[5]});
    };
    if (!crowd_control_rules::warpsEveryRemote(remote_players::isRemotePlayerHuman(frame->regs.ecx))) {
        frame->regs.eax = warp(frame->regs.ecx);
        return;
    }
    partner_scope::eachRemoteHuman([&](uintptr_t remote) {
        if (!remote) return;
        frame->regs.eax = warp(remote);
        if (activity_state::tracing()) {
            logger::write("trace: crowd: warp from 0x%X (call site) for remote 0x%X", frame->returnAddress, static_cast<unsigned>(remote));
        }
    });
}

PARTNER_REG_STUB(remoteHoldingEdiStub, edi, remoteHoldingCelebrity)
PARTNER_REG_STUB(nearestRemoteToCelebCarEsiStub, esi, nearestRemoteToCelebCar)
PARTNER_REG_STUB(nearestRemoteToRemoteStartEsiStub, esi, nearestRemoteToRemoteStart)
PARTNER_WRAP_STUB(teleportLocalOrEachRemoteStub, teleportLocalOrEachRemote, 0)

}  // namespace

namespace crowd_control_thunks {

partner_thunks::Binding bindSelect(Thunk thunk) {
    switch (thunk) {
        case Thunk::RemoteHoldingEdi: return {reinterpret_cast<void*>(&remoteHoldingEdiStub), nullptr};
        case Thunk::NearestRemoteToCelebCarEsi: return {reinterpret_cast<void*>(&nearestRemoteToCelebCarEsiStub), nullptr};
        case Thunk::NearestRemoteToRemoteStart: return {reinterpret_cast<void*>(&nearestRemoteToRemoteStartEsiStub), nullptr};
        case Thunk::TeleportLocalOrEachRemote: return {reinterpret_cast<void*>(&teleportLocalOrEachRemoteStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace crowd_control_thunks
