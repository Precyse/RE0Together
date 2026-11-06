#pragma once
#include "game_state_report_rule.h"
#include "net_client.h"

// GAME_STATE (protocol.h kGameState): sends the report of game_state_report_rule.h to the launcher when the link comes up
// and whenever a value changes.
namespace game_state_report {

// Once at start, before the net thread runs: co-op on or off (adapter.ini coop) and whether the save redirect is patched.
void configure(bool coop, bool saveRedirectInstalled);

// Net thread, every tick.
void onNetTick(NetClient& net, const SessionSnapshot& session);

}  // namespace game_state_report
