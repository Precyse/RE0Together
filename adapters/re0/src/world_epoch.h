#pragma once
#include <cstdint>

// The world epoch of partner_mode=state (rules: world_epoch_rule.h). The host counts the loads it makes and tells a
// guest the count with every JOIN_EXTRAS; the guest stamps FLAG_PROPOSE, TAKE_REQUEST, FLOOR_PUT and FLOOR_TAKE with the
// count of the snapshot it last applied (0 from its own load until the next snapshot), and the host drops what carries
// another one. Messages of other modes carry no stamp and are never judged.
namespace world_epoch {

// Game thread: this machine loaded a player save. The host starts a new world; a guest has left the one it adopted.
void onLocalLoad();

// Game thread, guest: the host's snapshot of epoch `epoch` was applied.
void adopt(uint16_t epoch);

// The epoch the host's JOIN_EXTRAS carries. Any thread.
uint16_t hostEpoch();

// What a message sent from this machine carries: the host's own epoch, a guest's adopted one. Any thread.
uint16_t stamp();

// Net thread: true when a message with this stamp may be used. On the host in state mode, a guest's message belongs to
// the current world only with the host's epoch as its stamp; a refused one is logged by `kind`. Always true on a guest
// and outside state mode.
bool acceptsFromGuest(uint16_t stamp, const char* kind);

}  // namespace world_epoch
