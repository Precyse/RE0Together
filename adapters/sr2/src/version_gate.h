#pragma once
#include <cstdint>
#include <string>

// Milestone 8: only peers running the same mod build play together (docs/SR2_NOTES.md, "Version gate"). Every join_req and
// join_acc ends with a trailer (version.h); the host refuses a join_req without a matching one (join_deny reason 1, the
// game's own "wrong game version"), and a guest refuses a join_acc without one the same way. On whenever the mod runs,
// at max_players 2 as well: an unmodded player is always refused. The host also refuses a join_req whose password is not
// the force join one (reason 7), and every join_deny a guest receives is noted.
namespace version_gate {

// Checks all sites, then writes them together (nothing when any differs). `maxPlayers` and `partnerExperimental` (the
// partner sites that are really patched) and `steamTransport` (the tunnel is installed) go into the trailer; `trace` logs
// each accept and refusal with its reason. Run before the trace hooks are enabled. True when installed.
bool install(uint32_t maxPlayers, bool partnerExperimental, bool steamTransport, bool trace);

// The password a join_req must carry to be admitted (force join, native transport); empty checks nothing.
void setJoinPassword(const std::string& password);

// The reason of the last join_deny this machine received (join_deny_rules::kReasonNone when none since the clear).
uint8_t lastJoinDeny();
void clearJoinDeny();

}  // namespace version_gate
