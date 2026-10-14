#include "activity_state.h"

#include <atomic>

#include "guest_call.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

std::atomic<uintptr_t> g_racePeer{0};
bool g_tracing = false;

}  // namespace

namespace activity_state {

void setTracing(bool tracing) { g_tracing = tracing; }

bool tracing() { return g_tracing; }

bool isHost() {
    const uintptr_t session = readDword(sr2::kSessionPointer);
    uint8_t flag = 0;
    return session && readMemory(session + sr2::kSessionHostFlagOffset, &flag, sizeof(flag)) && flag != 0;
}

int32_t runningActivityType() {
    const uintptr_t mission = readDword(sr2::kMissionPointer);
    if (!mission || readDword(mission + sr2::kMissionKindOffset) != sr2::kMissionKindActivity) return -1;
    const uintptr_t instance = readDword(mission + sr2::kMissionInstanceOffset);
    const uintptr_t vtable = instance ? readDword(instance) : 0;
    const uintptr_t typeIdFunction = vtable ? readDword(vtable + sr2::kInstanceTypeIdSlot) : 0;
    if (!typeIdFunction) return -1;
    return static_cast<int32_t>(guest_call::call(typeIdFunction, {.ecx = instance}));
}

uintptr_t senderOf(uintptr_t context) {
    return context ? readDword(context + sr2::kMessageContextMemberOffset) : 0;
}

size_t remoteMembersInGame(uintptr_t (&members)[activity_rules::kMaxMembers]) {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    size_t count = 0;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (!snapshot.members[i].local && snapshot.members[i].state == sr2::kMemberStateInGame) {
            members[count++] = snapshot.members[i].address;
        }
    }
    return count;
}

uint8_t raceRequestSub(uintptr_t message) {
    uint8_t flag = 0;
    readMemory(sr2::kRaceRequestFlag, &flag, sizeof(flag));
    uint8_t sub = 0;
    readMemory(message + sr2::kRaceRequestSubOffset + (flag ? 1 : 0), &sub, sizeof(sub));
    return sub;
}

void setRacePeer(uintptr_t member) { g_racePeer = member; }

uintptr_t takeRacePeer() { return g_racePeer.exchange(0); }

}  // namespace activity_state
