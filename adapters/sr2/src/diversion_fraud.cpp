// Insurance Fraud for every remote: the zone of a remote, the team hit and the tandem bonus go to the remote that earned them.
// The game's level keeps one zone, one "last object the partner hit" and sends its bonuses to "the partner".
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "diversion_stubs.h"
#include "diversion_thunks.h"
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

constexpr uint32_t kLowByte = 0xFF;

diversion_rules::TeamHits g_hits;
uintptr_t g_hitsRecord = 0;  // the level record the hits belong to: a new level starts them over
uintptr_t g_teammate = 0;    // the remote whose last hit is the local player's, after the last hit
uintptr_t g_tandem = 0;      // the remote the last tandem ragdoll matched

rate_limit::RateLimit g_zoneLimit;
rate_limit::RateLimit g_hitLimit;
rate_limit::OncePerKey g_tandemSeen;

// A remembered remote while it is still a remote player human, else the default partner.
uintptr_t rememberedOrDefault(uintptr_t remembered) {
    return remote_players::isRemotePlayerHuman(remembered) ? remembered : partner_scope::partnerHuman();
}

// DVF1: a human is in a fraud zone when it is a remote with a zone: the default partner's is the game's own, the others
// ask the zone lookup. `bl` holds the result of the tests before.
void __cdecl fraudZoneOfEsi(BlockFrame* frame) {
    const uintptr_t human = frame->regs.esi;
    uint32_t answer = frame->regs.ebx & kLowByte;
    if (remote_players::isRemotePlayerHuman(human)) {
        const uint32_t zone = human == partner_scope::partnerHuman() ? readDword(sr2::kFraudPartnerZone)
                                                                       : guest_call::call(sr2::kFraudZoneOfHuman, {}, {static_cast<uint32_t>(human)});
        if (diversion_trace::dueNow(g_zoneLimit)) {
            logger::write("trace: fraud: zone of human 0x%X is 0x%X", static_cast<unsigned>(human), zone);
        }
        if (zone != 0) answer = 1;
    }
    frame->regs.eax = (frame->regs.eax & ~kLowByte) | answer;
}

// DVF2: after a hit (ebx = the hitter, edi = the hit object, esi = the fraud level) the level's record gets the object the
// partner last hit: the teammate's, which is the local player's when some remote hit what the local player hit.
void __cdecl fraudTeamHit(BlockFrame* frame) {
    const uintptr_t level = frame->regs.esi;
    const uintptr_t record = level ? readDword(level + sr2::kFraudRecordOffset) : 0;
    if (!record) return;
    const uintptr_t hitter = frame->regs.ebx;
    const uint32_t object = frame->regs.edi;
    uint32_t& partnerHit = dwordField(record, sr2::kFraudPartnerHitOffset);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable()) {
        if (hitter == partner_scope::partnerHuman()) partnerHit = object;
        return;
    }
    if (record != g_hitsRecord) {
        g_hits.clear();
        g_hitsRecord = record;
        g_teammate = 0;
    }
    const size_t hitterAt = view.indexOf(hitter);
    if (hitterAt != partner_rules::kNone) g_hits.record(view.items()[hitterAt].slot, hitter, object);
    const uint32_t localHit = readDword(record + sr2::kFraudLocalHitOffset);
    const size_t mate = g_hits.teammate(view.items(), view.count(), view.defaultIndex, localHit);
    g_teammate = mate != partner_rules::kNone ? view.items()[mate].human : 0;
    const bool haveDefault = view.defaultIndex < view.count();
    partnerHit = mate != partner_rules::kNone ? localHit
                                              : (haveDefault ? g_hits.lastOf(view.items()[view.defaultIndex].slot, view.defaultHuman()) : 0);
    if (hitterAt != partner_rules::kNone && diversion_trace::dueNow(g_hitLimit)) {
        logger::write("trace: fraud: team hit by slot %u on 0x%X, teammate slot %d", view.items()[hitterAt].slot, object,
                      mate != partner_rules::kNone ? view.items()[mate].slot : -1);
    }
}

// DVF3: the team hit bonus goes to the teammate.
uintptr_t __cdecl fraudTeammate() { return rememberedOrDefault(g_teammate); }

// DVF10: the tandem bonus goes to the remote the tandem ragdoll matched.
uintptr_t __cdecl fraudTandemRemote() { return rememberedOrDefault(g_tandem); }

// DVF13: a ragdoll thrown by a remote is a player's, and that remote is the tandem remote.
uintptr_t __cdecl fraudTandemMatch(uintptr_t compared) {
    const uintptr_t matched = partner_identity::match(compared);
    if (matched == compared && compared != g_tandem) {
        g_tandem = compared;
        if (diversion_trace::firstTime(g_tandemSeen, compared)) {
            logger::write("trace: fraud: tandem remote 0x%X", static_cast<unsigned>(compared));
        }
    }
    return matched;
}

DIVERSION_BLOCK_STUB(fraudZoneOfEsiStub, fraudZoneOfEsi)
DIVERSION_BLOCK_STUB(fraudTeamHitStub, fraudTeamHit)
PARTNER_REG_STUB(fraudTandemMatchEbpStub, ebp, fraudTandemMatch)

}  // namespace

namespace diversion_thunks {

partner_thunks::Binding bindFraud(Thunk thunk) {
    switch (thunk) {
        case Thunk::FraudZoneOfEsi: return {reinterpret_cast<void*>(&fraudZoneOfEsiStub), nullptr};
        case Thunk::FraudTeamHitEbx: return {reinterpret_cast<void*>(&fraudTeamHitStub), nullptr};
        case Thunk::FraudTeammate: return {reinterpret_cast<void*>(&fraudTeammate), nullptr};
        case Thunk::FraudTandemRemote: return {reinterpret_cast<void*>(&fraudTandemRemote), nullptr};
        case Thunk::FraudTandemMatchEbp: return {reinterpret_cast<void*>(&fraudTandemMatchEbpStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace diversion_thunks
