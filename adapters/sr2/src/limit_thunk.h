#pragma once
#include <cstdint>

namespace limit_thunk {

// Retargets the call that activates the co-op allocator sets inside net_set_mode to a thunk that writes `maxPlayers`
// over both player limits first. The sets size the player simulation objects and the deltacomp baselines from the
// limit while they activate, which is before net_set_mode returns. True when the call was retargeted.
bool install(uint32_t maxPlayers);

}  // namespace limit_thunk
