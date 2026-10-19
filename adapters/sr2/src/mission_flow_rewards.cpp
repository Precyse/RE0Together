// Mission bonus counters for 3 and 4 players (MF18 to MF20) and the trace of the mission state (MF23): the act 7 bonus message names
// whose bonus it is for each recipient, the objective bonus goes to every remote.
#include <cstdio>
#include <string>

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
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
namespace rules = mission_flow_rules;

constexpr size_t kBufferArg = 0;
constexpr size_t kLengthArg = 1;
constexpr size_t kBonusArgCount = 3;
constexpr size_t kHandleTextChars = 32;

thread_local uintptr_t t_bonusHuman = 0;

void* g_bonusAwardOriginal = nullptr;
void* g_missionStateOriginal = nullptr;

// MF18: the human a bonus is for is in edx when the bonus function is entered; MF19 needs it where the message is sent.
void __cdecl rememberBonusHuman(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const ScopedValue<uintptr_t> human(t_bonusHuman, frame->regs.edx);
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_bonusAwardOriginal), frame->regs, {args[0], args[1], args[2]});
}

// MF19: the act 7 bonus message goes to every in-game remote member with its own flag: 1 when the bonus is that member's own. Called
// with edi = the session and (buffer, length) on the stack; the broadcast stands for one remote or a bonus not seen by MF18.
void __cdecl sendBonusPerRecipient(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    uint8_t* message = reinterpret_cast<uint8_t*>(args[kBufferArg]);
    const size_t flagAt = rules::bonusFlagOffset(readByte(sr2::kActHeaderWide) != 0);
    const bool perRecipient = t_bonusHuman && remote_players::remoteMemberCount() > rules::kShippedRemotes && message[flagAt - 1] == sr2::kActBonusSub;
    if (!perRecipient) {
        guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {args[kBufferArg], args[kLengthArg]});
        return;
    }
    uintptr_t members[activity_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(members);
    for (size_t i = 0; i < count; ++i) {
        message[flagAt] = rules::bonusFlag(t_bonusHuman, remote_players::humanOfMember(members[i]));
        guest_call::call(sr2::kSendToMember, {.esi = members[i]}, {args[kBufferArg], args[kLengthArg], 0, 0});
    }
    if (mission_flow::tracing()) {
        logger::write("trace: mission flow: bonus for human 0x%X sent to %zu members", static_cast<unsigned>(t_bonusHuman), count);
    }
}

// MF20: the objective bonus is awarded to the local player and to every remote player human, the game's function called once for each.
void __cdecl objectiveBonusEachRemote(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    size_t awarded = 0;
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        guest_call::call(sr2::kBonusAward, {.edx = human ? human : frame->regs.edx}, {args[0], args[1], args[2]});
        ++awarded;
    });
    if (mission_flow::tracing()) logger::write("trace: mission flow: objective bonus to %zu remote players", awarded);
}

// What an owner or partner handle names for the trace: the local player, the slot of a remote one, none or a stale handle.
std::string whoText(uint32_t handle) {
    if (!handle) return "none";
    const uintptr_t object = remote_players::objectOfHandle(handle);
    char text[kHandleTextChars];
    if (!object) {
        std::snprintf(text, sizeof(text), "dead handle 0x%X", static_cast<unsigned>(handle));
        return text;
    }
    if (object == remote_players::localHuman()) return "local";
    std::snprintf(text, sizeof(text), "slot %u", mission_flow::slotOf(remote_players::memberOfHuman(object)));
    return text;
}

// MF23: the mission state name is in esi; one line with the pair at every start, retry and replay.
void __cdecl traceMissionState(GuestFrame* frame) {
    if (!mission_flow::tracing()) return;
    logger::write("trace: mission flow: state \"%s\", owner %s, partner %s", reinterpret_cast<const char*>(frame->regs.esi),
                  whoText(readDword(sr2::kMissionOwnerHandle)).c_str(), whoText(readDword(sr2::kMissionPartnerHandle)).c_str());
}

PARTNER_WRAP_STUB(rememberBonusHumanStub, rememberBonusHuman, 0)
PARTNER_WRAP_STUB(sendBonusPerRecipientStub, sendBonusPerRecipient, 0)
PARTNER_WRAP_STUB(objectiveBonusEachRemoteStub, objectiveBonusEachRemote, 0)
TRACE_ENTRY_STUB(traceMissionStateStub, traceMissionState, g_missionStateOriginal)

}  // namespace

namespace mission_flow {

partner_thunks::Binding bindRewards(Thunk thunk) {
    switch (thunk) {
        case Thunk::RememberBonusHuman: return {reinterpret_cast<void*>(&rememberBonusHumanStub), &g_bonusAwardOriginal};
        case Thunk::SendBonusPerRecipient: return {reinterpret_cast<void*>(&sendBonusPerRecipientStub), nullptr};
        case Thunk::ObjectiveBonusEachRemote: return {reinterpret_cast<void*>(&objectiveBonusEachRemoteStub), nullptr};
        case Thunk::TraceMissionState: return {reinterpret_cast<void*>(&traceMissionStateStub), &g_missionStateOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace mission_flow
