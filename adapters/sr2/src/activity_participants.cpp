#include "activity_participants.h"

#include "activity_state.h"
#include "diversion_rules.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

// The getters ask on every call, so the static game cells are read directly, as remote_players.cpp does; a pointer the game may
// have freed is read through the safe reader.
uint32_t read32(uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); }

// The activity type is a constant of the instance's class, asked of the game once per new instance.
uintptr_t g_typeInstance = 0;
int32_t g_type = -1;

uintptr_t g_lastPin = 0;

// The player human a handle names, 0 when the handle is stale or empty (objectOfHandle with a direct read of the table).
uintptr_t humanOfHandle(uint32_t handle) {
    if (!handle) return 0;
    const uintptr_t object = read32(sr2::kObjectTable + (handle & sr2::kHandleIndexMask) * sr2::kObjectTableStride);
    return object && readDword(object + sr2::kObjectHandleOffset) == handle ? object : 0;
}

int32_t activityType(uintptr_t instance) {
    if (instance != g_typeInstance) {
        g_type = activity_state::runningActivityType();
        g_typeInstance = instance;
    }
    return g_type;
}

// The pin of this call, logged when it changes.
uintptr_t answer(uintptr_t pin, int32_t type, bool coopRace) {
    if (pin == g_lastPin) return pin;
    g_lastPin = pin;
    if (!activity_state::tracing()) return pin;
    if (!pin) {
        logger::write("trace: participants: pin released");
    } else if (coopRace) {
        logger::write("trace: participants: default partner pinned to human 0x%X (co-op race)", static_cast<unsigned>(pin));
    } else {
        logger::write("trace: participants: default partner pinned to human 0x%X (activity type %d)", static_cast<unsigned>(pin), type);
    }
    return pin;
}

}  // namespace

namespace activity_participants {

uintptr_t pinnedHuman() {
    const bool coopRace = *reinterpret_cast<const uint8_t*>(sr2::kCoopRaceFlag) != 0;
    const uintptr_t mission = read32(sr2::kMissionPointer);
    const bool activity = mission && read32(mission + sr2::kMissionKindOffset) == sr2::kMissionKindActivity;
    if (!coopRace && !activity) return answer(0, -1, false);
    const uintptr_t instance = activity ? read32(mission + sr2::kMissionInstanceOffset) : 0;
    const int32_t type = instance ? activityType(instance) : -1;
    const uintptr_t local = remote_players::localHuman();
    const uint32_t localHandle = local ? read32(local + sr2::kObjectHandleOffset) : 0;
    const uint32_t handle = diversion_rules::participantPin(localHandle, read32(sr2::kMissionOwnerHandle), read32(sr2::kMissionPartnerHandle),
                                                            diversion_rules::isTwoPlusS(type, coopRace));
    const uintptr_t human = humanOfHandle(handle);
    return answer(remote_players::isRemotePlayerHuman(human) ? human : 0, type, coopRace);
}

}  // namespace activity_participants
