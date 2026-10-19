#include "net_handlers.h"

#include <windows.h>

#include <iterator>
#include <string>

#include "activity_rules.h"
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
#include "session_kick_rules.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using net_handlers_rules::RaceCall;
using net_handlers_rules::RaceChange;
using partner_sites::Thunk;

// The place of each set in net_handlers_sites::kSets.
enum SetIndex : size_t { kSetBustedWait, kSetRaceHold, kSetReplay, kSetConfessRule, kSetConfessNotices, kSetConfessLock, kSetSessionLost, kSetCount };
static_assert(kSetCount == std::size(net_handlers_sites::kSets), "SetIndex names every set");

constexpr size_t kNameBlockChars = sr2::kMemberNameOffset / sizeof(wchar_t);  // the wide characters before the names in a member
constexpr size_t kReplayNamesChars = 0x100;                                   // the room the wait dialog's names have, with the NUL
constexpr char kUnprintableName = '?';
constexpr wchar_t kLastPrintable = 0x7E;
constexpr wchar_t kFirstPrintable = 0x20;

using MiscHandler = void(__cdecl*)(const int8_t* payload, uintptr_t context);

bool g_trace = false;
bool g_installed[kSetCount] = {};
net_handlers_rules::BustedWaits g_busted;
rate_limit::RateLimit g_noSenderLimit;
activity_rules::DeclineTally g_declines;
void* g_bustedClearOriginal = nullptr;
void* g_replayAnswerOriginal = nullptr;
void* g_replayPromptOriginal = nullptr;
void* g_replayStartOriginal = nullptr;
wchar_t g_waitBlock[kNameBlockChars + kReplayNamesChars] = {};  // the game reads the joined names at +0xCC, as it reads a member's name

uintptr_t currentSession() { return readDword(sr2::kSessionPointer); }

int32_t clockNow() { return static_cast<int32_t>(readDword(sr2::kGameClock)); }

uintptr_t hostMember() {
    const uintptr_t session = currentSession();
    return session ? readDword(session + sr2::kSessionHostMemberOffset) : 0;
}

// ---- Busted wait ----

// A remote human is waiting when its member's busted wait has not run out. Any other human is not.
bool remoteWaiting(uintptr_t human) {
    const uintptr_t member = remote_players::memberOfHuman(human);
    return member && g_busted.waiting(currentSession(), member, clockNow());
}

// BW1: 0x8CBDB0 for a human that is not the local one (the game's own test answers for the local human).
uint32_t __cdecl bustedWaitAsked(uintptr_t human) {
    const bool waiting = remoteWaiting(human);
    if (g_trace) {
        logger::write("trace: busted: waiting asked for slot %u: %u", net_handlers::memberSlot(remote_players::memberOfHuman(human)), waiting ? 1u : 0u);
    }
    return waiting ? 1 : 0;
}

// BW2: misc_19 on the host. The notoriety clears when the host and every other remote are waiting; otherwise the sender's wait is kept.
uintptr_t __cdecl bustedHostMessage() {
    const uintptr_t sender = net_handlers::senderMember();
    const uintptr_t senderHuman = sender ? remote_players::humanOfMember(sender) : 0;
    const bool localWaiting = (guest_call::call(sr2::kWaitingToRespawn, {.esi = remote_players::localHuman()}) & 0xFF) != 0;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const bool clears = net_handlers_rules::hostClears(localWaiting, snapshot.pairing, senderHuman, remoteWaiting);
    const net_handlers_rules::OthersCount others = net_handlers_rules::countOthers(snapshot.pairing, senderHuman, remoteWaiting);
    if (clears) {
        g_busted.clear();
        if (g_trace) {
            logger::write("trace: busted: slot %u busted (host), local waiting %u, other remotes waiting %zu of %zu, notoriety cleared",
                          net_handlers::memberSlot(sender), localWaiting ? 1u : 0u, others.matching, others.total);
        }
        return sr2::kBustedClear;
    }
    const int32_t deadline = net_handlers_rules::deadlineAfter(clockNow());
    if (sender) g_busted.record(currentSession(), sender, deadline);
    if (g_trace) {
        logger::write("trace: busted: slot %u busted (host), local waiting %u, other remotes waiting %zu of %zu, recorded until %d",
                      net_handlers::memberSlot(sender), localWaiting ? 1u : 0u, others.matching, others.total, deadline);
    }
    return sr2::kBustedRecord;
}

// BW3: misc_19 on every machine. A guest keeps the sender's wait; a downed local bleeds out when every other remote is out too.
uintptr_t __cdecl bustedLocalMessage() {
    const uintptr_t sender = net_handlers::senderMember();
    const int32_t now = clockNow();
    if (!activity_state::isHost() && sender) {
        const int32_t deadline = net_handlers_rules::deadlineAfter(now);
        g_busted.record(currentSession(), sender, deadline);
        if (g_trace) logger::write("trace: busted: slot %u recorded until %d", net_handlers::memberSlot(sender), deadline);
    }
    const uintptr_t local = remote_players::localHuman();
    if (!local || !net_handlers_rules::pending(remote_players::downedTimerOf(local), now)) return sr2::kBustedReturn;
    const uintptr_t senderHuman = sender ? remote_players::humanOfMember(sender) : 0;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const bool bleeds = net_handlers_rules::localBleedsOut(true, snapshot.pairing, senderHuman, remote_players::isDownOrWaiting);
    if (g_trace) {
        const net_handlers_rules::OthersCount others = net_handlers_rules::countOthers(snapshot.pairing, senderHuman, remote_players::isDownOrWaiting);
        logger::write("trace: busted: slot %u busted, local down, other remotes out %zu of %zu, %s", net_handlers::memberSlot(sender),
                      others.matching, others.total, bleeds ? "local bleeds out" : "stays down");
    }
    return bleeds ? sr2::kBustedBleedOut : sr2::kBustedReturn;
}

// BW4: the host's own bust clears the notoriety only when every remote is waiting.
uint32_t __cdecl bustedRemotesWaiting() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const bool clears = net_handlers_rules::hostBustClears(snapshot.pairing, remoteWaiting);
    if (g_trace) {
        const net_handlers_rules::OthersCount remotes = net_handlers_rules::countOthers(snapshot.pairing, 0, remoteWaiting);
        logger::write("trace: busted: local busted (host), remotes waiting %zu of %zu, %s", remotes.matching, remotes.total,
                      clears ? "notoriety cleared" : "wait 30 s");
    }
    return clears ? 1 : 0;
}

// BW5: after every game clear (0x8CBD90).
void __cdecl bustedCleared() {
    const size_t emptied = g_busted.clear();
    if (g_trace) logger::write("trace: busted: game cleared, wait table emptied (%zu entries)", emptied);
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

// ---- Mission replay ----

// The remote members in the game other than `except`.
size_t remotesInGameExcept(uintptr_t except, uintptr_t (&members)[activity_rules::kMaxMembers]) {
    const size_t found = activity_state::remoteMembersInGame(members);
    size_t kept = 0;
    for (size_t i = 0; i < found; ++i) {
        if (members[i] != except) members[kept++] = members[i];
    }
    return kept;
}

// RP1: misc_1b, an answer to the replay prompt. A guest takes only the host's. The host's first decline would end the wait for
// everyone; it passes only once every remote in the game declined.
void __cdecl replayAnswer(const int8_t* payload, uintptr_t context) {
    const uintptr_t sender = activity_state::senderOf(context);
    const bool host = activity_state::isHost();
    const uint32_t wait = readDword(sr2::kReplayWait);
    const int8_t answer = *payload;
    uintptr_t inGame[activity_rules::kMaxMembers];
    const size_t inGameCount = host && wait ? activity_state::remoteMembersInGame(inGame) : 0;
    const bool declinedAll = net_handlers_rules::countsDecline(host, wait != 0, answer) && g_declines.declinedAll(wait, sender, inGame, inGameCount);
    const net_handlers_rules::Verdict verdict = net_handlers_rules::replayVerdict(host, sender, hostMember(), answer, wait != 0, declinedAll);
    const bool dropped = verdict == net_handlers_rules::Verdict::Drop;
    if (g_trace && !host && dropped) {
        logger::write("trace: replay: misc_1b from slot %u (not the host) dropped", net_handlers::memberSlot(sender));
    }
    if (g_trace && host && wait) {
        logger::write("trace: replay: slot %u answered %d, declined %zu of %zu in game, %s", net_handlers::memberSlot(sender), answer,
                      g_declines.declinedCount(wait), inGameCount, dropped ? "held" : "passed");
    }
    if (!dropped) reinterpret_cast<MiscHandler>(g_replayAnswerOriginal)(payload, context);
}

// misc_1a and misc_1c: a guest takes them only from the host.
void relayFromHost(const char* name, void* original, const int8_t* payload, uintptr_t context) {
    const uintptr_t sender = activity_state::senderOf(context);
    if (!net_handlers_rules::guestTakes(activity_state::isHost(), sender, hostMember())) {
        if (g_trace) logger::write("trace: replay: %s from slot %u (not the host) dropped", name, net_handlers::memberSlot(sender));
        return;
    }
    reinterpret_cast<MiscHandler>(original)(payload, context);
}

void __cdecl replayPrompt(const int8_t* payload, uintptr_t context) { relayFromHost("misc_1a", g_replayPromptOriginal, payload, context); }

void __cdecl replayStart(const int8_t* payload, uintptr_t context) { relayFromHost("misc_1c", g_replayStartOriginal, payload, context); }

std::string asciiOf(const wchar_t* text) {
    std::string ascii;
    for (; *text != 0; ++text) ascii += *text >= kFirstPrintable && *text <= kLastPrintable ? static_cast<char>(*text) : kUnprintableName;
    return ascii;
}

// RP2: the block the wait dialog's text formatter reads a name from (+0xCC): the remote members in the game, in slot order, joined.
uintptr_t __cdecl replayWaitNamesBlock() {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    remote_players::MemberInfo inGame[partner_rules::kMaxWalked];
    size_t inGameCount = 0;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (snapshot.members[i].state == sr2::kMemberStateInGame) inGame[inGameCount++] = snapshot.members[i];
    }
    session_kick_rules::Entry ordered[partner_rules::kMaxWalked];
    const size_t count = session_kick_rules::remotesBySlot(inGame, inGameCount, ordered);
    wchar_t names[partner_rules::kMaxWalked][session_kick_rules::kMemberNameChars] = {};
    const wchar_t* pointers[partner_rules::kMaxWalked];
    for (size_t i = 0; i < count; ++i) {
        readMemory(ordered[i].member + sr2::kMemberNameOffset, names[i], sizeof(names[i]) - sizeof(wchar_t));
        pointers[i] = names[i];
    }
    wchar_t* joined = g_waitBlock + kNameBlockChars;
    net_handlers_rules::joinNames(pointers, count, joined, kReplayNamesChars);
    if (g_trace) logger::write("trace: replay: waiting for \"%s\"", asciiOf(joined).c_str());
    return reinterpret_cast<uintptr_t>(g_waitBlock);
}

// A remote left while the wait is open: it counts as a decline, and the wait is cancelled when every remote that remains declined.
void replayLeft(uintptr_t leaver) {
    const uint32_t wait = readDword(sr2::kReplayWait);
    if (!wait) return;
    uintptr_t remaining[activity_rules::kMaxMembers];
    const size_t remainingCount = remotesInGameExcept(leaver, remaining);
    const bool allDeclined = g_declines.declinedAll(wait, leaver, remaining, remainingCount);
    if (!net_handlers_rules::replayCancelsOnLeave(true, remainingCount, allDeclined)) return;
    if (g_trace) logger::write("trace: replay: slot %u left the wait, every remaining remote declined, cancelled", net_handlers::memberSlot(leaver));
    guest_call::call(sr2::kReplayWaitCancel, guest_call::Regs{});
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

PARTNER_REG_STUB(bustedWaitAskedStub, esi, bustedWaitAsked)
PARTNER_RESUME_STUB(bustedHostMessageStub, bustedHostMessage)
PARTNER_RESUME_STUB(bustedLocalMessageStub, bustedLocalMessage)
PARTNER_RESUME_STUB(raceHoldAcquireStub, raceHoldAcquire)
PARTNER_RESUME_STUB(raceHoldReleaseStub, raceHoldRelease)

// Detour of the game's clear (0x8CBD90): the clear runs, then the wait table is emptied, every register as the clear left it.
void __declspec(naked) bustedClearStub() {
    __asm {
        call dword ptr [g_bustedClearOriginal]
        pushad
        call bustedCleared
        popad
        ret
    }
}

}  // namespace

namespace net_handlers {

uintptr_t senderMember() {
    const uintptr_t sender = sender_scope::currentSender();
    return sender ? sender : partner_scope::partnerMember();
}

uint8_t memberSlot(uintptr_t member) { return member ? readByte(member + sr2::kMemberSlotOffset) : partner_rules::kNoSlot; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::BustedWaitAsked: return {reinterpret_cast<void*>(&bustedWaitAskedStub), nullptr};
        case Thunk::BustedHostMessage: return {reinterpret_cast<void*>(&bustedHostMessageStub), nullptr};
        case Thunk::BustedLocalMessage: return {reinterpret_cast<void*>(&bustedLocalMessageStub), nullptr};
        case Thunk::BustedRemotesWaiting: return {reinterpret_cast<void*>(&bustedRemotesWaiting), nullptr};
        case Thunk::BustedClearHook: return {reinterpret_cast<void*>(&bustedClearStub), &g_bustedClearOriginal};
        case Thunk::RaceHoldAcquire: return {reinterpret_cast<void*>(&raceHoldAcquireStub), nullptr};
        case Thunk::RaceHoldRelease: return {reinterpret_cast<void*>(&raceHoldReleaseStub), nullptr};
        case Thunk::ReplayAnswer: return {reinterpret_cast<void*>(&replayAnswer), &g_replayAnswerOriginal};
        case Thunk::ReplayWaitNames: return {reinterpret_cast<void*>(&replayWaitNamesBlock), nullptr};
        case Thunk::ReplayPrompt: return {reinterpret_cast<void*>(&replayPrompt), &g_replayPromptOriginal};
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
    g_busted.forget(leaver);
    if (g_installed[kSetRaceHold]) raceHoldLeft(leaver);
    if (g_installed[kSetReplay]) replayLeft(leaver);
    net_handlers_confess::memberLeft(leaver);
}

}  // namespace net_handlers
