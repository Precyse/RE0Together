#pragma once
#include <cstdint>

// Rules of the world epoch (partner_mode=state, docs/RE0_NOTES.md "State mode: world epoch"): the host counts the loads
// it makes; a guest stamps what it sends with the epoch of the last host snapshot it applied, and the host drops what
// carries another one. A request from before a load names a spot in a room, not a world, so only the stamp can tell it
// from the same request made in the reloaded world. Pure, no game access, unit tested.
namespace world_epoch_rule {

constexpr uint16_t kNone = 0;  // not stamped, or no host snapshot adopted yet: never a host epoch
constexpr uint16_t kFirstEpoch = 1;

// The epoch after `epoch`; skips kNone when the counter wraps.
constexpr uint16_t next(uint16_t epoch) { return epoch == UINT16_MAX ? kFirstEpoch : static_cast<uint16_t>(epoch + 1); }

// Host: a guest's message is for this world only when its stamp is the host's epoch.
constexpr bool current(uint16_t stamp, uint16_t hostEpoch) { return stamp == hostEpoch; }

}  // namespace world_epoch_rule
