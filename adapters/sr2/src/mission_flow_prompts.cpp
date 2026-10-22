// Mission start and replay prompts for 3 and 4 players (MF1 to MF17): who asked and who answers, the first accept that starts the
// mission, the remote a restart keeps, the replay answer tally and the names a waiting dialog shows.
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

#include "activity_rules.h"
#include "activity_state.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "mission_flow.h"
#include "mission_flow_rules.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "scoped_value.h"
#include "sender_scope.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
namespace rules = mission_flow_rules;

constexpr size_t kBufferArg = 0;
constexpr size_t kLengthArg = 1;
constexpr size_t kContextArg = 1;
constexpr size_t kPromptEventArg = 2;
constexpr size_t kStartReplyReasonIndex = 4;  // past the 4 byte mission id of the mssn_req
constexpr size_t kReplayAnswerIndex = 1;      // past the sub id of the misc message
constexpr size_t kTargetTextChars = 24;

// The member that asked for a mission and the id it asked for, kept while the host's prompt for it is open.
struct StartRequester {
    uintptr_t member;
    uint32_t pendingId;
};

// The member that asked for a replay and the prompt the host opened for it.
struct ReplayRequester {
    uintptr_t member;
    uint32_t dialog;
};

StartRequester g_startRequester{};
ReplayRequester g_replayRequester{};
activity_rules::DeclineTally g_replayTally;
std::mutex g_replayTallyLock;  // the tally is touched by the misc handler (MF15) and the member left callback
bool g_trace = false;
thread_local bool t_answeringStart = false;
thread_local uintptr_t t_explicitTarget = 0;
thread_local char16_t t_joinedNames[rules::kJoinedNameChars];

void* g_startPromptOriginal = nullptr;
void* g_replayRequestOriginal = nullptr;
void* g_replayAnswerOriginal = nullptr;

const uintptr_t g_acceptResume = sr2::kAcceptResume;
const uintptr_t g_acceptSkip = sr2::kAcceptSkip;

rules::Prompt promptState() { return {readDword(sr2::kStartPromptDialog), readByte(sr2::kStartPromptDeferred) != 0}; }

// The start requester while its question is still the pending one and it is still a remote member, else 0.
uintptr_t liveStartRequester() {
    const StartRequester& asked = g_startRequester;
    if (!asked.member || readDword(sr2::kPendingMissionId) != asked.pendingId || !remote_players::isRemoteMember(asked.member)) return 0;
    return asked.member;
}

// The replay requester while the host's prompt for it is still the open one and it is still a remote member, else 0.
uintptr_t liveReplayRequester() {
    const ReplayRequester& asked = g_replayRequester;
    if (!asked.member || readDword(sr2::kReplayDialog) != asked.dialog || !remote_players::isRemoteMember(asked.member)) return 0;
    return asked.member;
}

rules::SendInputs sendInputs(uintptr_t remembered) {
    return {activity_state::isHost(), remote_players::remoteMemberCount(), t_explicitTarget, sender_scope::currentSender(), remembered,
            remote_players::hostMember()};
}

// Sends the message of a retargeted broadcast call (args: buffer, length; edi: the session) to `target`.
void send(const rules::Target& target, const uint32_t* args, uintptr_t session) {
    if (target.kind == rules::TargetKind::Member) {
        guest_call::call(sr2::kSendToMember, {.esi = target.member}, {args[kBufferArg], args[kLengthArg], 0, 0});
    } else {
        guest_call::call(sr2::kSendBroadcast, {.edi = session}, {args[kBufferArg], args[kLengthArg]});
    }
}

std::string targetText(const rules::Target& target) {
    if (target.kind == rules::TargetKind::Broadcast) return "every member";
    if (target.member == remote_players::hostMember()) return "the host";
    char text[kTargetTextChars];
    std::snprintf(text, sizeof(text), "slot %u", mission_flow::slotOf(target.member));
    return text;
}

std::string slotList(const uintptr_t* members, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) text += ", ";
        text += std::to_string(mission_flow::slotOf(members[i]));
    }
    return text;
}

uint8_t messageByte(const uint32_t* args, uintptr_t wideHeaderFlag, size_t index) {
    return readByte(args[kBufferArg] + rules::payloadOffset(readByte(wideHeaderFlag) != 0, index));
}

// MF1: the prompt callback runs on the host outside any message scope. While it runs, MF2, MF3 and MF5 know that the question is
// answered, and an answer ends the question.
void __cdecl startPromptAnswer(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    {
        const ScopedValue<bool> answering(t_answeringStart, true);
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_startPromptOriginal), frame->regs,
                                           {args[0], args[1], args[kPromptEventArg]});
    }
    const int32_t event = static_cast<int32_t>(args[kPromptEventArg]);
    if (!rules::answersPrompt(event)) return;
    if (g_trace) {
        logger::write("trace: mission flow: start prompt answered (%s)", event == rules::kPromptEventAccept ? "accept" : "decline");
    }
    g_startRequester = {};
}

// MF2, MF3: the owner of a mission a guest asked for is that guest, not the default partner.
uintptr_t __cdecl startRequesterHuman() {
    const uintptr_t fallback = partner_scope::partnerHuman();
    if (!t_answeringStart || !activity_state::isHost() || remote_players::remoteMemberCount() <= rules::kShippedRemotes) return fallback;
    const uintptr_t member = liveStartRequester();
    const uintptr_t human = member ? remote_players::humanOfMember(member) : 0;
    if (g_trace && g_startRequester.member) {
        if (human) {
            logger::writeUnlessRepeated("trace: mission flow: start owner is the requester in slot %u", mission_flow::slotOf(member));
        } else {
            logger::writeUnlessRepeated("trace: mission flow: start owner: requester in slot %u is gone, the default partner",
                                        mission_flow::slotOf(g_startRequester.member));
        }
    }
    return human ? human : fallback;
}

// MF4: the host's handler of a guest's ask. When it opens a prompt, the guest that asked is remembered with the id it asked for.
void __cdecl rememberStartRequester(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const rules::Prompt before = promptState();
    guest_call::call(sr2::kStartRequest, frame->regs, {args[0]});
    if (!rules::promptOpened(before, promptState())) return;
    const uintptr_t member = sender_scope::currentSender();
    if (!remote_players::isRemoteMember(member)) return;
    g_startRequester = {member, readDword(sr2::kPendingMissionId)};
    if (g_trace) {
        logger::write("trace: mission flow: start request from slot %u opened a prompt for mission 0x%X", mission_flow::slotOf(member),
                      static_cast<unsigned>(g_startRequester.pendingId));
    }
}

// MF5: a start reply goes to the one member it is for. Called with edi = the session and (buffer, length) on the stack.
void __cdecl sendStartReply(GuestFrame* frame) {
    const uintptr_t remembered = t_answeringStart ? liveStartRequester() : 0;
    const rules::Target target = rules::sendTarget(sendInputs(remembered));
    if (g_trace && remote_players::remoteMemberCount() > rules::kShippedRemotes) {
        logger::write("trace: mission flow: start reply %u to %s", messageByte(frame->args, sr2::kStartHeaderWide, kStartReplyReasonIndex),
                      targetText(target).c_str());
    }
    send(target, frame->args, frame->regs.edi);
}

// The cancel of the host's own ask, to every in-game remote member except the one that accepted: their prompts close. Fills
// `closed` with those members and returns how many.
size_t cancelOtherPrompts(uintptr_t accepter, uintptr_t (&closed)[activity_rules::kMaxMembers]) {
    uintptr_t inGame[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(inGame);
    size_t cancelled = 0;
    for (size_t i = 0; i < count; ++i) {
        if (inGame[i] == accepter) continue;
        closed[cancelled++] = inGame[i];
        const ScopedValue<uintptr_t> target(t_explicitTarget, inGame[i]);
        guest_call::call(sr2::kStartReply, {}, {0});
    }
    return cancelled;
}

// MF6: nonzero when the host goes on with an accept (the first one), 0 when it drops it.
uint32_t __cdecl firstAcceptStarts(uint32_t noMission, uint32_t acceptedId) {
    const size_t remotes = remote_players::remoteMemberCount();
    const rules::AcceptDecision decision =
        rules::acceptDecision(readDword(sr2::kPendingMissionId), noMission, readDword(sr2::kMissionPointer) != 0, remotes);
    const uintptr_t accepter = sender_scope::currentSender();
    if (decision == rules::AcceptDecision::Skip) return 0;
    if (decision == rules::AcceptDecision::SkipLate) {
        if (g_trace) {
            logger::write("trace: mission flow: accept from slot %u for mission 0x%X ignored, a mission is already set up",
                          mission_flow::slotOf(accepter), static_cast<unsigned>(acceptedId));
        }
        return 0;
    }
    if (remotes > rules::kShippedRemotes) {
        uintptr_t closed[activity_rules::kMaxMembers];
        const size_t count = cancelOtherPrompts(accepter, closed);
        if (g_trace) {
            logger::write("trace: mission flow: accept from slot %u starts mission 0x%X, prompts closed for slots {%s}",
                          mission_flow::slotOf(accepter), static_cast<unsigned>(acceptedId), slotList(closed, count).c_str());
        }
    }
    return 1;
}

// Entered by a jump over the test of the pending id (eax = the no-mission id, esi = the accepted id); continues at the accept path
// or at the end of the handler.
void __declspec(naked) firstAcceptStub() {
    __asm {
        push esi
        push eax
        call firstAcceptStarts
        add esp, 8
        test eax, eax
        jz dropped
        jmp dword ptr [g_acceptResume]
    dropped:
        jmp dword ptr [g_acceptSkip]
    }
}

// MF7 to MF10: a retry, replay or intro takes the remote from the pair if one is in it, so a mission a guest owns stays that guest's.
uintptr_t __cdecl pairRemoteHuman() {
    const uintptr_t fallback = partner_scope::partnerHuman();
    if (partner_scope::overrideActive()) return fallback;
    const uintptr_t owner = guest_call::call(sr2::kMissionOwnerHuman, {});
    const uintptr_t partner = guest_call::call(sr2::kMissionPartnerHuman, {});
    const uintptr_t human = rules::pairRemote(owner, partner, remote_players::localHuman(), remote_players::isRemotePlayerHuman, fallback);
    if (g_trace && human != fallback) {
        logger::writeUnlessRepeated("trace: mission flow: restart keeps the pair's remote in slot %u (the default partner is slot %u)",
                                    mission_flow::slotOf(remote_players::memberOfHuman(human)),
                                    mission_flow::slotOf(remote_players::memberOfHuman(fallback)));
    }
    return human;
}

// The wide names at `addresses` joined for a waiting dialog; the text lives in a thread buffer.
const char16_t* joinedNames(const uintptr_t* addresses, size_t count) {
    std::u16string_view names[partner_rules::kMaxWalked];
    for (size_t i = 0; i < count; ++i) names[i] = std::u16string_view(reinterpret_cast<const char16_t*>(addresses[i]), sr2::kNameChars);
    rules::joinNames(names, count, t_joinedNames, std::size(t_joinedNames));
    return t_joinedNames;
}

// MF11: the wait dialog of the host's ask names every remote human it asked. The result is the name's address (eax).
uintptr_t __cdecl askedHumanNames() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    if (!activity_state::isHost() || snapshot.humanCount <= rules::kShippedRemotes) {
        return partner_scope::partnerHuman() + sr2::kHumanNameOffset;
    }
    uintptr_t addresses[partner_rules::kMaxWalked];
    for (size_t i = 0; i < snapshot.humanCount; ++i) addresses[i] = snapshot.humans[i].human + sr2::kHumanNameOffset;
    if (g_trace) logger::writeUnlessRepeated("trace: mission flow: waiting dialog names %zu players", snapshot.humanCount);
    return reinterpret_cast<uintptr_t>(joinedNames(addresses, snapshot.humanCount));
}

// MF17: the same for the replay wait dialog, from the members in the game.
uintptr_t __cdecl askedMemberNames() {
    uintptr_t members[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(members);
    if (!activity_state::isHost() || count <= rules::kShippedRemotes) return partner_scope::partnerMember() + sr2::kMemberNameOffset;
    uintptr_t addresses[activity_rules::kMaxMembers];
    for (size_t i = 0; i < count; ++i) addresses[i] = members[i] + sr2::kMemberNameOffset;
    if (g_trace) logger::writeUnlessRepeated("trace: mission flow: waiting dialog names %zu players", count);
    return reinterpret_cast<uintptr_t>(joinedNames(addresses, count));
}

// MF12, MF16: a guest's replay request and replay start go to the host only; the host's request stays the broadcast.
void __cdecl sendGuestToHost(GuestFrame* frame) {
    const rules::Target target = rules::sendTarget(sendInputs(0));
    if (g_trace && target.kind == rules::TargetKind::Member) logger::write("trace: mission flow: replay message sent to the host only");
    send(target, frame->args, frame->regs.edi);
}

// MF13: the host's handler of a replay ask. When it opens a prompt, the guest that asked is remembered with that prompt.
void __cdecl rememberReplayRequester(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uint32_t before = readDword(sr2::kReplayDialog);
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_replayRequestOriginal), frame->regs,
                                       {args[kBufferArg], args[kContextArg]});
    const uint32_t after = readDword(sr2::kReplayDialog);
    const uintptr_t member = sender_scope::currentSender();
    if (!activity_state::isHost() || after == before || !after || !remote_players::isRemoteMember(member)) return;
    g_replayRequester = {member, after};
    if (g_trace) logger::write("trace: mission flow: replay request from slot %u opened a prompt", mission_flow::slotOf(member));
}

// MF14: a replay answer goes to the member it is for: the host from a guest, the asking guest from the host's prompt.
void __cdecl sendReplayAnswer(GuestFrame* frame) {
    const uintptr_t remembered = readByte(sr2::kReplayAsker) == 0 ? liveReplayRequester() : 0;
    const rules::Target target = rules::sendTarget(sendInputs(remembered));
    if (g_trace && remote_players::remoteMemberCount() > rules::kShippedRemotes) {
        logger::write("trace: mission flow: replay answer %u to %s", messageByte(frame->args, sr2::kReplayHeaderWide, kReplayAnswerIndex),
                      targetText(target).c_str());
    }
    send(target, frame->args, frame->regs.edi);
}

// True when the host asked for the replay, more than one remote could answer and the answer came from a member.
bool tallyApplies(uintptr_t member) {
    return member && activity_state::isHost() && readByte(sr2::kReplayAsker) != 0 && readDword(sr2::kReplayDialog) != 0 &&
           remote_players::remoteMemberCount() > rules::kShippedRemotes;
}

// MF15: the host's ask for a replay goes on with the first accept, and with a decline only once every remote member in the game has
// declined. The game's handler of the answer runs for the answers that count.
void __cdecl replayAnswerTally(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uintptr_t member = activity_state::senderOf(args[kContextArg]);
    bool applies = true;
    if (tallyApplies(member)) {
        const int32_t answer = static_cast<int8_t>(readByte(args[kBufferArg]));
        uintptr_t required[activity_rules::kMaxMembers];
        const size_t count = activity_state::remoteMembersInGame(required);
        bool declinedAll = false;
        size_t declined = 0;
        {
            const std::lock_guard<std::mutex> lock(g_replayTallyLock);
            declinedAll = answer != sr2::kReplayAnswerAccept && g_replayTally.declinedAll(readDword(sr2::kReplayDialog), member, required, count);
            declined = g_replayTally.declinedCount();
        }
        applies = rules::replayAnswerApplies(answer, declinedAll);
        if (g_trace) {
            logger::write("trace: mission flow: replay answer %d from slot %u, declined %zu of %zu, %s", answer, mission_flow::slotOf(member),
                          declined, count, applies ? "applied" : "withheld");
        }
    }
    if (applies) {
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_replayAnswerOriginal), frame->regs,
                                           {args[kBufferArg], args[kContextArg]});
    }
}

// The leave rule of the host's replay wait: a remote that leaves without answering counts as a decline. When the remotes that stay have
// all declined, the wait is cancelled the way the game does at its own leave teardown, which only runs for the last remote.
void replayWaitLeaverDeclines(uintptr_t leaver) {
    if (!activity_state::isHost() || readByte(sr2::kReplayAsker) == 0 || readDword(sr2::kReplayDialog) == 0) return;
    uintptr_t inGame[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(inGame);
    activity_rules::DeclineTally::LeaveOutcome outcome{};
    size_t declined = 0;
    {
        const std::lock_guard<std::mutex> lock(g_replayTallyLock);
        outcome = g_replayTally.leave(readDword(sr2::kReplayDialog), leaver, inGame, count);
        declined = g_replayTally.declinedCount();
    }
    if (outcome.staying == 0) return;
    if (g_trace) {
        logger::write("trace: mission flow: slot %u left the replay wait: counted as a decline, %zu declined, %zu remaining -> %s",
                      mission_flow::slotOf(leaver), declined, outcome.staying, outcome.ends ? "cancelled" : "still waiting");
    }
    if (outcome.ends) guest_call::call(sr2::kReplayCancel, {});
}

PARTNER_WRAP_STUB(startPromptAnswerStub, startPromptAnswer, 0)
PARTNER_WRAP_STUB(rememberStartRequesterStub, rememberStartRequester, 0)
PARTNER_WRAP_STUB(sendStartReplyStub, sendStartReply, 0)
PARTNER_WRAP_STUB(sendGuestToHostStub, sendGuestToHost, 0)
PARTNER_WRAP_STUB(rememberReplayRequesterStub, rememberReplayRequester, 0)
PARTNER_WRAP_STUB(sendReplayAnswerStub, sendReplayAnswer, 0)
PARTNER_WRAP_STUB(replayAnswerTallyStub, replayAnswerTally, 0)

}  // namespace

namespace mission_flow {

void setTrace(bool trace) { g_trace = trace; }

bool tracing() { return g_trace; }

unsigned slotOf(uintptr_t member) { return member ? readByte(member + sr2::kMemberSlotOffset) : 0; }

void memberLeft(uintptr_t leaver) {
    if (g_startRequester.member == leaver) {
        g_startRequester = {};
        if (g_trace) logger::write("trace: mission flow: slot %u left: forgot the start requester", slotOf(leaver));
    }
    if (g_replayRequester.member == leaver) {
        g_replayRequester = {};
        if (g_trace) logger::write("trace: mission flow: slot %u left: forgot the replay requester", slotOf(leaver));
    }
    if (activity_state::leaverCountsAsDecline(leaver)) replayWaitLeaverDeclines(leaver);
    const uintptr_t human = remote_players::humanOfMember(leaver);
    if (!g_trace || !human) return;
    const uint32_t handle = readDword(human + sr2::kObjectHandleOffset);
    if (handle == readDword(sr2::kMissionOwnerHandle)) {
        logger::write("trace: mission flow: slot %u left: it was the pair's owner, the pair keeps a dead handle", slotOf(leaver));
    }
    if (handle == readDword(sr2::kMissionPartnerHandle)) {
        logger::write("trace: mission flow: slot %u left: it was the pair's partner, the pair keeps a dead handle", slotOf(leaver));
    }
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::StartPromptAnswer: return {reinterpret_cast<void*>(&startPromptAnswerStub), &g_startPromptOriginal};
        case Thunk::StartRequesterHuman: return {reinterpret_cast<void*>(&startRequesterHuman), nullptr};
        case Thunk::RememberStartRequester: return {reinterpret_cast<void*>(&rememberStartRequesterStub), nullptr};
        case Thunk::SendStartReply: return {reinterpret_cast<void*>(&sendStartReplyStub), nullptr};
        case Thunk::FirstAcceptStarts: return {reinterpret_cast<void*>(&firstAcceptStub), nullptr};
        case Thunk::PairRemoteHuman: return {reinterpret_cast<void*>(&pairRemoteHuman), nullptr};
        case Thunk::AskedHumanNames: return {reinterpret_cast<void*>(&askedHumanNames), nullptr};
        case Thunk::SendGuestToHost: return {reinterpret_cast<void*>(&sendGuestToHostStub), nullptr};
        case Thunk::RememberReplayRequester: return {reinterpret_cast<void*>(&rememberReplayRequesterStub), &g_replayRequestOriginal};
        case Thunk::SendReplayAnswer: return {reinterpret_cast<void*>(&sendReplayAnswerStub), nullptr};
        case Thunk::ReplayAnswerTally: return {reinterpret_cast<void*>(&replayAnswerTallyStub), &g_replayAnswerOriginal};
        case Thunk::AskedMemberNames: return {reinterpret_cast<void*>(&askedMemberNames), nullptr};
        default: return bindRewards(thunk);
    }
}

}  // namespace mission_flow
