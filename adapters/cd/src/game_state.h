#pragma once
// GAME_STATE to our own launcher (game_state_rule.h): where this game is, whether other players' copies are shown and
// whether each peer's adapter matches ours. Sent when the link is up and whenever the text changes. Main tick only.

namespace game_state {

// At start-up, once: adapter.ini presence=1 and whether init found every message name the copies use.
void setPresence(bool configured, bool namesFound);

void tick();

}  // namespace game_state
