#pragma once
#include <cstddef>
#include <cstdint>

#include "activity_rules.h"

// Reads of the running activity and the session role, for the milestone 7 thunks.
namespace activity_state {

// With trace=1 the milestone 7 thunks log their decisions.
void setTracing(bool tracing);
bool tracing();

// True when this machine hosts the co-op session.
bool isHost();

// The type id of the running activity (docs/SR2_NOTES.md "Activities"), -1 when no activity is running.
int32_t runningActivityType();

// The member a message came from: [ctx+4] of the context a message handler gets, 0 when there is none.
uintptr_t senderOf(uintptr_t context);

// The decline of a request from a member that left, laid out as the game's answer handlers read a decline message: the reason byte
// (0 = MISSION_PARTNER_DECLINED), a cursor at 0 and the message context whose member sits at +4. The handlers take the parts by address.
class LeaverDecline {
public:
    explicit LeaverDecline(uintptr_t leaver) { context_[1] = static_cast<uint32_t>(leaver); }

    // Not const: the handler stores through the cursor (0x6A8983), so the object must not be a const one.
    uint32_t message() { return addressOf(message_); }
    uint32_t cursor() { return addressOf(&cursor_); }
    uint32_t context() { return addressOf(context_); }

private:
    static uint32_t addressOf(const void* part) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(part)); }

    uint8_t message_[1] = {0};
    uint32_t cursor_ = 0;
    uint32_t context_[2] = {0, 0};
};

// True when a member that leaves without answering a request counts as a decline: it is a remote member, not this machine's own.
bool leaverCountsAsDecline(uintptr_t leaver);

// The remote members that are in the game, written to `members`; their count.
size_t remoteMembersInGame(uintptr_t (&members)[activity_rules::kMaxMembers]);

// The sub id of a race_req message the game has built (the byte after the header; one later when the flag byte is set).
uint8_t raceRequestSub(uintptr_t message);

// The member that invited this machine to a race and waits for the answer, 0 when none. The answer is sent from a
// dialog, outside the message handler, so the member is remembered here.
void setRacePeer(uintptr_t member);
uintptr_t takeRacePeer();

}  // namespace activity_state
