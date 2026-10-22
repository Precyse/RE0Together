// Thunks of homies and followers (milestone 16 wave 2): the second leader of a leader's extra follower (PA1), the mode enter that
// dismisses the followers and clears the attachment of every remote (PA2, PA3) and the trace of the follower hand-over (PT1).

#include <vector>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "party_ai_rules.h"
#include "party_ai_thunks.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

bool g_trace = false;
void* g_followerAddOriginal = nullptr;
rate_limit::OncePerKey g_defaultSecondLeaders;  // the followers whose second leader was the default partner

bool isHidden(uintptr_t human) { return (dwordField(human, sr2::kHumanVisibilityFlagsOffset) & sr2::kHumanHiddenBit) != 0; }

// The remotes that can take a follower now: not busy, not hidden.
party_ai_rules::HumanList available(const party_ai_rules::HumanList& remotes) {
    party_ai_rules::HumanList free;
    for (const uintptr_t human : remotes.span()) {
        if (!party_ai_thunks::humanIsBusy(human) && !isHidden(human)) free.add(human);
    }
    return free;
}

// PA1: the second leader earmarked for a follower of a player's, in place of the call of get_remote_player. The leader's second and
// later followers go round the remotes that can take one (all of them when none can), so players 3 and 4 get a share; with fewer
// than two remotes, an untrusted pairing or while a handler answers for one member, it is the default partner as shipped.
uintptr_t __cdecl secondLeaderShare(uintptr_t leader, uintptr_t follower) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const party_ai_rules::HumanList remotes = party_ai_rules::remoteHumansOf(snapshot.pairing);
    if (party_ai_rules::shippedPath(snapshot.pairing.trusted, remotes.count) || partner_scope::overrideActive()) {
        if (g_trace && g_defaultSecondLeaders.firstTime(follower)) {
            FpuGuard guard;
            logger::write("trace: party: follower 0x%08X has second leader the default partner", static_cast<unsigned>(follower));
        }
        return partner_scope::partnerHuman();
    }
    const party_ai_rules::HumanList free = available(remotes);
    const party_ai_rules::HumanList& pool = free.count > 0 ? free : remotes;
    const uint32_t followers = dwordField(leader, sr2::kHumanFollowerCountOffset);
    const size_t share = party_ai_rules::secondLeaderRank(followers, pool.count);
    const uintptr_t chosen = pool.items[share];
    if (g_trace) {
        FpuGuard guard;
        logger::write("trace: party: follower 0x%08X of 0x%08X (follower %u) has second leader slot %u (share %zu of %zu)",
                      static_cast<unsigned>(follower), static_cast<unsigned>(leader), followers,
                      party_ai_rules::slotOfHuman(snapshot.pairing, chosen), share, pool.count);
    }
    return chosen;
}

// PA2: the derby enter dismisses the followers of every remote (ebx is 0, the flag the game passes).
void __cdecl dismissEachRemoteFollowers(GuestFrame* frame) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    std::vector<uint8_t> slots;
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human) return;
        guest_call::call(sr2::kDismissFollowers, {.edi = human}, {frame->regs.ebx});
        slots.push_back(party_ai_rules::slotOfHuman(snapshot.pairing, human));
    });
    if (g_trace) {
        FpuGuard guard;
        logger::write("trace: party: activity type %d, dismissed the followers of slots %s",
                      static_cast<int>(readDword(sr2::kGameplayModeType)), party_ai_rules::slotListText(slots).c_str());
    }
}

// PA3: the Septic Avenger enter clears the attachment of every remote that has one, as it does the local player's.
void __cdecl clearEachRemoteAttachment(GuestFrame* frame) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    std::vector<uint8_t> slots;
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human) return;
        const uintptr_t attachment = dwordField(human, sr2::kHumanAttachmentOffset);
        if (!attachment) return;
        byteField(attachment, sr2::kAttachmentFlagOffset) = static_cast<uint8_t>(frame->regs.ebx);
        dwordField(attachment, sr2::kAttachmentLinkOffset) = frame->regs.ebx;
        guest_call::call(sr2::kUnlinkAttachment, {.eax = attachment});
        slots.push_back(party_ai_rules::slotOfHuman(snapshot.pairing, human));
    });
    if (g_trace) {
        FpuGuard guard;
        logger::write("trace: party: activity type %d, cleared the attachment of slots %s", sr2::kModeTypeSepticAvenger,
                      party_ai_rules::slotListText(slots).c_str());
    }
}

// PT1 (trace=1): the follower add. The tick of the followers hands a follower to its second leader through it; that one is logged.
void __cdecl traceFollowerAdd(GuestFrame* frame) {
    if (frame->returnAddress != sr2::kFollowerHandOverReturn) return;
    FpuGuard guard;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    logger::write("trace: party: follower 0x%08X joins its second leader 0x%08X (slot %u)", frame->args[1], frame->args[0],
                  party_ai_rules::slotOfHuman(snapshot.pairing, frame->args[0]));
}

// Replaces `call get_remote_player` where esi is the leader and ebx the follower that gets the second leader.
void __declspec(naked) secondLeaderShareStub() {
    __asm {
        push ebx
        push esi
        call secondLeaderShare
        add esp, 8
        ret
    }
}

PARTNER_WRAP_STUB(dismissEachRemoteFollowersStub, dismissEachRemoteFollowers, 0)
PARTNER_WRAP_STUB(clearEachRemoteAttachmentStub, clearEachRemoteAttachment, 0)
TRACE_ENTRY_STUB(followerAddStub, traceFollowerAdd, g_followerAddOriginal)

}  // namespace

namespace party_ai_thunks {

void setTrace(bool trace) { g_trace = trace; }

bool traceOn() { return g_trace; }

bool humanIsBusy(uintptr_t human) { return (guest_call::call(sr2::kHumanIsBusy, {}, {human}) & 0xFF) != 0; }

partner_thunks::Binding bindParty(Thunk thunk) {
    switch (thunk) {
        case Thunk::SecondLeaderShare: return {reinterpret_cast<void*>(&secondLeaderShareStub), nullptr};
        case Thunk::DismissEachRemoteFollowers: return {reinterpret_cast<void*>(&dismissEachRemoteFollowersStub), nullptr};
        case Thunk::ClearEachRemoteAttachment: return {reinterpret_cast<void*>(&clearEachRemoteAttachmentStub), nullptr};
        case Thunk::TraceFollowerAdd: return {reinterpret_cast<void*>(&followerAddStub), &g_followerAddOriginal};
        default: return {nullptr, nullptr};
    }
}

partner_thunks::Binding bind(Thunk thunk) {
    for (const auto bindArea : {&bindParty, &bindNpcAi, &bindTeamOdds}) {
        const partner_thunks::Binding binding = bindArea(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace party_ai_thunks
