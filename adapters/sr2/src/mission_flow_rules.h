#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "sr2_addresses.h"

// The pure rules of milestone 16 W2 mission flow (no game access, unit tested; docs/SR2_NOTES.md, "Mission flow"): who a start or
// replay reply goes to, when a start request opened a prompt, which accept starts the mission, which remote a restart keeps, when
// a replay answer counts, the key of a decline tally, the joined names of a waiting dialog and the flag of a bonus message.
namespace mission_flow_rules {

// With at most this many remote members every rule is the shipped behaviour.
constexpr size_t kShippedRemotes = 1;

// The events of the start prompt callback 0x6A8450 (arg3) that answer the prompt.
constexpr int32_t kPromptEventDecline = 1;
constexpr int32_t kPromptEventAccept = 2;

// The characters of a joined name list the waiting dialogs take, and the text between two names.
constexpr size_t kJoinedNameChars = 0x60;
constexpr std::u16string_view kNameSeparator = u" / ";

enum class TargetKind : uint8_t { Broadcast, Member };

struct Target {
    TargetKind kind;
    uintptr_t member;
};

struct SendInputs {
    bool isHost;
    size_t remoteMembers;
    uintptr_t explicitMember;        // a send that names its member (the cancels of the first accept)
    uintptr_t scopedMember;          // the sender of the message the handler on this thread is handling
    uintptr_t rememberedRequester;   // the member that asked, while its question is answered from a dialog
    uintptr_t hostMember;
};

// Where a start or replay message goes. One remote: the game's broadcast. Otherwise an explicit member, on a guest the host,
// on the host the sender being handled, then the remembered requester, else the broadcast.
inline Target sendTarget(const SendInputs& in) {
    constexpr Target kBroadcast{TargetKind::Broadcast, 0};
    if (in.remoteMembers <= kShippedRemotes) return kBroadcast;
    if (in.explicitMember) return {TargetKind::Member, in.explicitMember};
    if (!in.isHost) return in.hostMember ? Target{TargetKind::Member, in.hostMember} : kBroadcast;
    if (in.scopedMember) return {TargetKind::Member, in.scopedMember};
    if (in.rememberedRequester) return {TargetKind::Member, in.rememberedRequester};
    return kBroadcast;
}

struct Prompt {
    uint32_t dialog;
    bool deferred;
};

// A start request opened a prompt when the dialog changed to a new one or the prompt was put off until later.
inline bool promptOpened(const Prompt& before, const Prompt& after) {
    return (after.dialog != before.dialog && after.dialog != 0) || (!before.deferred && after.deferred);
}

enum class AcceptDecision : uint8_t { Skip, SkipLate, Start };

// What the host does with an accept: nothing when no mission is pending, nothing when a mission is already set up and more than one
// remote is there (a later accept), else it starts.
inline AcceptDecision acceptDecision(uint32_t pending, uint32_t noMission, bool missionSet, size_t remoteMembers) {
    if (pending == noMission) return AcceptDecision::Skip;
    if (remoteMembers > kShippedRemotes && missionSet) return AcceptDecision::SkipLate;
    return AcceptDecision::Start;
}

// The remote a restart keeps: the first of owner and partner that is a remote player, else the fallback.
template <typename IsRemote>
uintptr_t pairRemote(uintptr_t owner, uintptr_t partner, uintptr_t local, IsRemote isRemote, uintptr_t fallback) {
    for (const uintptr_t human : {owner, partner}) {
        if (human && human != local && isRemote(human)) return human;
    }
    return fallback;
}

inline bool answersPrompt(int32_t event) { return event == kPromptEventDecline || event == kPromptEventAccept; }

// The asker of a replay goes on with an accept at once, and with a decline only once every remote has declined.
inline bool replayAnswerApplies(int32_t answer, bool declinedAll) { return answer == sr2::kReplayAnswerAccept || declinedAll; }

// The members except `leaver`, in order, into `out` (room for `count`); the number copied.
inline size_t withoutMember(const uintptr_t* members, size_t count, uintptr_t leaver, uintptr_t* out) {
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        if (members[i] != leaver) out[kept++] = members[i];
    }
    return kept;
}

// A leave ends the host's replay wait when remotes stay in the game and all of them have declined. With none left the game's own
// leave teardown closes the wait.
inline bool replayLeaveCancels(size_t remaining, bool remainingDeclined) { return remaining > 0 && remainingDeclined; }

// A new wait dialog starts a new tally; the pending id stands in while there is none.
inline uint32_t declineKey(uint32_t waitDialog, uint32_t pendingId) { return waitDialog ? waitDialog : pendingId; }

// The names joined by kNameSeparator into `out` (capacity `capacity` characters, terminated), each cut at its terminator or after
// sr2::kNameChars characters, the whole cut at the capacity. The number of characters written, the terminator not counted.
inline size_t joinNames(const std::u16string_view* names, size_t count, char16_t* out, size_t capacity) {
    if (capacity == 0) return 0;
    size_t length = 0;
    const auto put = [&](char16_t c) {
        if (length + 1 < capacity) out[length++] = c;
    };
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            for (const char16_t c : kNameSeparator) put(c);
        }
        for (size_t at = 0; at < names[i].size() && at < sr2::kNameChars && names[i][at] != u'\0'; ++at) put(names[i][at]);
    }
    out[length] = u'\0';
    return length;
}

// Where byte `index` of a message's payload is: after the 3 byte header (4 with the wide header flag of that message).
constexpr size_t kHeaderBytes = 3;
inline size_t payloadOffset(bool wideHeader, size_t index) { return kHeaderBytes + (wideHeader ? 1 : 0) + index; }

// The byte of the act 7 message that says whose bonus it is: the one after the sub id, which is the first byte of the payload.
constexpr size_t kBonusFlagIndex = 1;
inline size_t bonusFlagOffset(bool wideHeader) { return payloadOffset(wideHeader, kBonusFlagIndex); }

// 1 when the bonus belongs to the recipient's own player.
inline uint8_t bonusFlag(uintptr_t eventHuman, uintptr_t recipientHuman) { return eventHuman == recipientHuman ? 1 : 0; }

}  // namespace mission_flow_rules
