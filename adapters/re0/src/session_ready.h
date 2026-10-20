#pragma once

// The session-ready handshake of partner_mode=state (docs/RE0_NOTES.md "State mode: session"; rules in
// session_ready_rule.h). After a guest applied the host's join snapshot and its extras it tells the host which room and load
// it stands in (SESSION_READY 0x0155); the host accepts when the guest's own newest pose agrees and echoes it. The guest is
// caught up (join_sync::caughtUp) when the echo has arrived, and only then sends its status stream and inventories.
namespace session_ready {

// Registers SESSION_READY with the frame router (policy Any: the direction is checked by role) and the per-tick handshake.
// False when the type could not be registered.
bool enable();

// Guest: the host's echo has arrived since the last reset. Any thread.
bool echoed();

// Guest: the game was loaded again or the link dropped; the handshake starts over. Game thread.
void reset();

}  // namespace session_ready
