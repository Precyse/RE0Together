#include "coop_autosave.h"

#include <windows.h>

#include "activity_state.h"
#include "coop_autosave_rules.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "player_visuals_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

coop_autosave_rules::Timer g_timer;  // the game thread only

bool inGame() {
    const uintptr_t own = remote_players::localMember();
    return own && readDword(own + sr2::kMemberStateOffset) == sr2::kMemberStateInGame;
}

bool playerUp() {
    const uintptr_t player = readDword(sr2::kLocalPlayer);
    return player && player_visuals_rules::healthIsPositive(readDword(player + sr2::kHumanHealthOffset)) &&
           remote_players::downedTimerOf(player) < 0;
}

coop_autosave_rules::World world() {
    return {
        .guest = readDword(sr2::kSessionPointer) != 0 && !activity_state::isHost(),
        .inGame = inGame(),
        .saving = readByte(sr2::kSaveInFlight) != 0,
        .missionType = static_cast<int32_t>(guest_call::call(sr2::kMissionTypeNow, {})),
        .held = readByte(sr2::kSavesSuppressed) != 0 || readByte(sr2::kAutosaveAfterFade) != 0,
        .cutscene = readDword(sr2::kCutscene) != 0,
        .paused = readDword(sr2::kPauseRefCount) != 0,
        .playerUp = playerUp(),
        .deviceDialog = coop_autosave_rules::deviceDialog(static_cast<int32_t>(readDword(sr2::kSaveCount)),
                                                          readByte(sr2::kAutosaveListed) != 0, readByte(sr2::kNoSaveDevice) != 0),
    };
}

}  // namespace

namespace coop_autosave {

void tick() {
    if (!g_timer.due(GetTickCount64(), world())) return;
    logger::write("autosave: %u min of free roam since this guest's last save, the game autosaves",
                  static_cast<unsigned>(coop_autosave_rules::kIntervalMs / coop_autosave_rules::kMsPerMinute));
    guest_call::call(sr2::kAutosaveRequest, {});
}

}  // namespace coop_autosave
