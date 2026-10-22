#include "activity_declines.h"

#include <mutex>

#include "activity_rules.h"
#include "activity_state.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "mission_flow_rules.h"
#include "sr2_addresses.h"

namespace {

const uintptr_t g_declineHandler = sr2::kDeclineHandler;

void* g_zombieAnswerOriginal = nullptr;

// The tallies are touched by the misc handlers (AF6, AC14) and the member left callback.
std::mutex g_lock;
activity_rules::DeclineTally g_startTally;
activity_rules::DeclineTally g_zombieTally;

// The key of the host's start wait: its dialog, else the pending mission id.
uint32_t startKey() { return mission_flow_rules::declineKey(readDword(sr2::kStartWaitDialog), readDword(sr2::kPendingMissionId)); }

// The leave rule of the host's start wait (AF6): the game's decline handler is entered with edx = the message and (cursor, context)
// on the stack, as the host's decline branch does.
void startWaitLeaverDeclines(uintptr_t leaver) {
    if (readDword(sr2::kStartWaitDialog) == 0) return;
    uintptr_t inGame[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(inGame);
    activity_rules::DeclineTally::LeaveOutcome outcome{};
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        outcome = g_startTally.leave(startKey(), leaver, inGame, count);
    }
    if (activity_state::tracing()) {
        logger::write("trace: start wait: member 0x%X left, counted as a decline, %zu remaining -> %s", static_cast<unsigned>(leaver), outcome.staying,
                      outcome.ends ? "every remote has declined, the start ends" : "the start goes on");
    }
    if (!outcome.ends || !activity_rules::declineHandlerCanRun(readDword(sr2::kLocalPlayer))) return;
    activity_state::LeaverDecline decline(leaver);
    guest_call::call(g_declineHandler, {.edx = decline.message()}, {decline.cursor(), decline.context()});
}

// The leave rule of the zombie request (AC14): the game's answer handler is entered with (message, context) for a decline.
void zombieRequestLeaverDeclines(uintptr_t leaver) {
    const uint32_t dialog = readDword(sr2::kZombieDialog);
    if (!g_zombieAnswerOriginal || dialog == 0) return;
    uintptr_t inGame[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(inGame);
    activity_rules::DeclineTally::LeaveOutcome outcome{};
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        outcome = g_zombieTally.leave(dialog, leaver, inGame, count);
    }
    if (activity_state::tracing()) {
        logger::write("trace: zombie request: member 0x%X left, counted as a decline, %zu remaining -> %s", static_cast<unsigned>(leaver),
                      outcome.staying, outcome.ends ? "every remote has declined" : "waiting for the others");
    }
    if (!outcome.ends) return;
    activity_state::LeaverDecline decline(leaver);
    guest_call::call(reinterpret_cast<uintptr_t>(g_zombieAnswerOriginal), {}, {decline.message(), decline.context()});
}

}  // namespace

namespace activity_declines {

bool startDeclined(uintptr_t member) {
    uintptr_t required[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    const uint32_t pending = readDword(sr2::kPendingMissionId);
    bool all = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        all = g_startTally.declinedAll(startKey(), member, required, count);
    }
    if (activity_state::tracing()) {
        logger::write("trace: start declined by member 0x%X for request %u, %s", static_cast<unsigned>(member), pending,
                      all ? "every remote has declined, the start ends" : "the start goes on");
    }
    return all;
}

bool zombieDeclined(uint32_t dialog, uintptr_t member) {
    uintptr_t required[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    bool all = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        all = g_zombieTally.declinedAll(dialog, member, required, count);
    }
    if (activity_state::tracing()) {
        logger::write("trace: zombie request declined by member 0x%X, %s", static_cast<unsigned>(member),
                      all ? "every remote has declined" : "waiting for the others");
    }
    return all;
}

void** zombieAnswerOriginal() { return &g_zombieAnswerOriginal; }

void memberLeft(uintptr_t leaver) {
    if (!activity_state::isHost() || !activity_state::leaverCountsAsDecline(leaver)) return;
    startWaitLeaverDeclines(leaver);
    zombieRequestLeaverDeclines(leaver);
}

}  // namespace activity_declines
