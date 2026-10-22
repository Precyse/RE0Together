#include "player_cap.h"

#include "code_patch.h"
#include "guest_welcome.h"
#include "hooks.h"
#include "home_network.h"
#include "leave_path.h"
#include "look_reset.h"
#include "log.h"
#include "player_cap_patches.h"
#include "remote_players.h"
#include "spawn_budget.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

// isCoop's answer for any member count: some member other than the local one is ready. With 2 members this is the
// shipped test; a member still joining is ignored instead of turning co-op off for the players already in.
uint32_t __cdecl someMemberReady(uintptr_t session) {
    return remote_players::anyRemoteMemberAtLeast(session, sr2::kMemberStateReady) ? 1 : 0;
}

// Jumped to from inside isCoop, which has no stack frame there: the return goes to isCoop's caller.
PARTNER_REG_STUB(coopMemberReadyStub, ecx, someMemberReady)

// The hooks of the M5 set: the leave function and the spawn group create.
bool installHooks() {
    if (!leave_path::installHook()) return false;
    if (spawn_budget::installHook()) return true;
    leave_path::removeHook();
    return false;
}

}  // namespace

namespace player_cap {

bool install(uint32_t maxPlayers, bool trace) {
    home_network::setTrace(trace);
    guest_welcome::setTrace(trace);
    look_reset::setTrace(trace);
    spawn_budget::setTrace(trace);
    leave_path::setTrace(trace);
    const leave_path::Thunks leave = leave_path::thunks();
    const player_cap_patches::Thunks thunks{reinterpret_cast<uintptr_t>(&coopMemberReadyStub),
                                            home_network::duplicateJoinSenderThunk(), guest_welcome::thunk(),
                                            look_reset::resetIndexLooksThunk(), look_reset::traceTintSendThunk(),
                                            leave.dropLeaverRecord, leave.keepRename};
    const auto patches = player_cap_patches::forPlayers(maxPlayers, thunks, trace);
    if (patches.empty()) return false;
    // The hooks are queued only once every site is known to match, and taken out if the edits still fail.
    if (!codeMatches(patches) || !installHooks()) {
        logger::write("player cap: sites differ, the maximum stays 2");
        return false;
    }
    if (!applyCodePatches(patches)) {
        leave_path::removeHook();
        spawn_budget::removeHook();
        logger::write("player cap: the edits could not be written, the maximum stays 2");
        return false;
    }
    logger::write("player cap: session maximum %u, isCoop counts ready members, join_req duplicate search retargeted, "
                  "guests welcome late joiners, player looks start empty, member leave reissues objects, group creates fit the spawn pools",
                  maxPlayers);
    return true;
}

}  // namespace player_cap
