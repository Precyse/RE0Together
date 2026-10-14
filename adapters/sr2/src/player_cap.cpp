#include "player_cap.h"

#include "code_patch.h"
#include "guest_welcome.h"
#include "home_network.h"
#include "log.h"
#include "player_cap_patches.h"
#include "remote_players.h"
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

}  // namespace

namespace player_cap {

bool install(uint32_t maxPlayers, bool trace) {
    home_network::setTrace(trace);
    guest_welcome::setTrace(trace);
    const player_cap_patches::Thunks thunks{reinterpret_cast<uintptr_t>(&coopMemberReadyStub),
                                            home_network::duplicateJoinSenderThunk(), guest_welcome::thunk()};
    const auto patches = player_cap_patches::forPlayers(maxPlayers, thunks);
    if (patches.empty()) return false;
    if (!applyCodePatches(patches)) {
        logger::write("player cap: sites differ, the maximum stays 2");
        return false;
    }
    logger::write("player cap: session maximum %u, isCoop counts ready members, join_req duplicate search retargeted, guests welcome late joiners", maxPlayers);
    return true;
}

}  // namespace player_cap
