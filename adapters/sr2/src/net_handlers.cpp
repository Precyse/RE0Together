#include "net_handlers.h"

#include <windows.h>

#include <iterator>

#include "activity_state.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "net_handlers_confess.h"
#include "net_handlers_rules.h"
#include "net_handlers_sites.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sender_scope.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using net_handlers_rules::RaceCall;
using net_handlers_rules::RaceChange;
using partner_sites::Thunk;

// The place of each set in net_handlers_sites::kSets.
enum SetIndex : size_t { kSetRaceHold, kSetReplayStart, kSetConfessRule, kSetConfessNotices, kSetConfessLock, kSetSessionLost, kSetCount };
static_assert(kSetCount == std::size(net_handlers_sites::kSets), "SetIndex names every set");

using MiscHandler = void(__cdecl*)(const int8_t* payload, uintptr_t context);

bool g_trace = false;
bool g_installed[kSetCount] = {};
rate_limit::RateLimit g_noSenderLimit;
void* g_replayStartOriginal = nullptr;

uintptr_t currentSession() { return readDword(sr2::kSessionPointer); }

uintptr_t hostMember() {
    const uintptr_t session = currentSession();
    return session ? readDword(session + sr2::kSessionHostMemberOffset) : 0;
}

// ---- Race hold ----

uint8_t& raceRemoteHold() { return byteField(sr2::kRaceHoldRemote, 0); }

uint8_t raceLocalHold() { return byteField(sr2::kRaceHoldLocal, 0); }

const char* raceCallText(RaceCall call) {
    switch (call) {
        case RaceCall::Load: return "load";
        case RaceCall::Unload: return "unload";
        default: return "no change";
    }
}

void traceRace(const char* event, uint8_t slot, const RaceChange& change) {
    if (!g_trace) return;
    logger::write("trace: race hold: slot %u %s, mask 0x%02X -> 0x%02X, local %u, %s", slot, event, change.before, change.after,
                  raceLocalHold(), raceCallText(change.call));
}

// The sender of a race hold message, or 0 after a line (rate limited) that there is none.
uintptr_t raceSender() {
    const uintptr_t sender = net_handlers::senderMember();
    uint32_t messages = 0;
    if (!sender && g_noSenderLimit.allow(GetTickCount64(), messages)) {
        logger::write("race hold: message without a sender member, ignored (%u since the last line)", messages);
    }
    return sender;
}

// RH1: scrpt_02 with a hold. The game loads the set only for the first holder.
uintptr_t __cdecl raceHoldAcquire() {
    const uintptr_t sender = raceSender();
    if (!sender) return sr2::kRaceHoldReturn;
    const uint8_t slot = net_handlers::memberSlot(sender);
    const RaceChange change = net_handlers_rules::raceAcquire(raceRemoteHold(), raceLocalHold(), slot);
    raceRemoteHold() = change.after;
    traceRace("holds", slot, change);
    return change.call == RaceCall::Load ? sr2::kRaceHoldLoad : sr2::kRaceHoldReturn;
}

// RH2: scrpt_03 with a release. The game unloads the set only for the last holder.
uintptr_t __cdecl raceHoldRelease() {
    const uintptr_t sender = raceSender();
    if (!sender) return sr2::kRaceHoldSkip;
    const uint8_t slot = net_handlers::memberSlot(sender);
    const RaceChange change = net_handlers_rules::raceRelease(raceRemoteHold(), raceLocalHold(), slot);
    raceRemoteHold() = change.after;
    traceRace("released", slot, change);
    return change.call == RaceCall::Unload ? sr2::kRaceHoldUnload : sr2::kRaceHoldSkip;
}

// A leaver lets go of the set; the unload the game would not do for it is done here.
void raceHoldLeft(uintptr_t leaver) {
    const uint8_t slot = net_handlers::memberSlot(leaver);
    const RaceChange change = net_handlers_rules::raceRelease(raceRemoteHold(), raceLocalHold(), slot);
    raceRemoteHold() = change.after;
    traceRace("left", slot, change);
    if (change.call == RaceCall::Unload) guest_call::call(sr2::kRaceResourceRelease, {.esi = sr2::kRaceResourceSet});
}

// ---- Mission replay start ----

// RP4: misc_1c, the start handshake. A guest takes it only from the host.
void __cdecl replayStart(const int8_t* payload, uintptr_t context) {
    const uintptr_t sender = activity_state::senderOf(context);
    if (!net_handlers_rules::guestTakes(activity_state::isHost(), sender, hostMember())) {
        if (g_trace) logger::write("trace: replay: misc_1c from slot %u (not the host) dropped", net_handlers::memberSlot(sender));
        return;
    }
    reinterpret_cast<MiscHandler>(g_replayStartOriginal)(payload, context);
}

// ---- Session lost name ----

// SL1: the human the session lost dialog names.
uintptr_t __cdecl sessionLostHuman() {
    const bool host = activity_state::isHost();
    const uintptr_t member = hostMember();
    const uintptr_t hostHuman = !host && member ? remote_players::humanOfMember(member) : 0;
    const uintptr_t chosen = net_handlers_rules::sessionLostChoice(host, hostHuman, partner_scope::partnerHuman());
    if (g_trace && !host) {
        if (hostHuman) {
            logger::write("trace: session lost: named the host's human 0x%08X (slot %u)", static_cast<unsigned>(hostHuman), net_handlers::memberSlot(member));
        } else {
            logger::write("trace: session lost: the host has no human, generic text");
        }
    }
    return chosen;
}

PARTNER_RESUME_AT_STUB(raceHoldAcquireStub, raceHoldAcquire)
PARTNER_RESUME_AT_STUB(raceHoldReleaseStub, raceHoldRelease)

}  // namespace

namespace net_handlers {

uintptr_t senderMember() {
    const uintptr_t sender = sender_scope::currentSender();
    return sender ? sender : partner_scope::partnerMember();
}

uint8_t memberSlot(uintptr_t member) { return member ? readByte(member + sr2::kMemberSlotOffset) : partner_rules::kNoSlot; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::RaceHoldAcquire: return {reinterpret_cast<void*>(&raceHoldAcquireStub), nullptr};
        case Thunk::RaceHoldRelease: return {reinterpret_cast<void*>(&raceHoldReleaseStub), nullptr};
        case Thunk::ReplayStart: return {reinterpret_cast<void*>(&replayStart), &g_replayStartOriginal};
        case Thunk::SessionLostHuman: return {reinterpret_cast<void*>(&sessionLostHuman), nullptr};
        default: return net_handlers_confess::bind(thunk);
    }
}

void install(bool trace) {
    g_trace = trace;
    net_handlers_confess::setTrace(trace);
    for (size_t i = 0; i < kSetCount; ++i) {
        const net_handlers_sites::Set& set = net_handlers_sites::kSets[i];
        if (!site_installer::sitesMatch(set.sites)) {
            logger::write("net handlers: %s not patched, the bytes differ", set.name);
            continue;
        }
        const site_installer::Result result = site_installer::install(set.sites, false, &bind);
        g_installed[i] = result.installed;
        if (result.installed) {
            logger::write("net handlers: %s, %zu sites patched", set.name, result.patched);
        } else {
            logger::write("net handlers: %s not patched", set.name);
        }
    }
}

void memberLeft(uintptr_t leaver) {
    if (g_installed[kSetRaceHold]) raceHoldLeft(leaver);
    net_handlers_confess::memberLeft(leaver);
}

}  // namespace net_handlers
