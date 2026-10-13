#pragma once
// DS2-internal: the guest-side vetoes of the BT grab (docs/DS2_NOTES.md, "BT grab"). A guest's puppet BTs are asleep, but an
// awake puppet's animation events still fire: its gazer catch event would send the guest's own player into the catcher
// (MsgRequestPlayerInteractiveAction, kind 2), and a request from a puppet would grab the guest's Sam. Only the host's BTs
// decide who is grabbed, so both are dropped on a guest unless the adapter itself is applying the host's state. The relay of the
// host's grab to the guest (BT_GRAB 0x0138, BT_GRAB_STATE 0x0139) is not built: it needs live probes first.
#include <cstdint>

namespace bt_grab {

// Start-up (adapter.ini enemy_sync=1): detours on the gazer catch event (0x1419cacf0) and on the player state's handler of the
// interactive action request (0x140e1db40), each installed only when the function starts with the bytes it was written for.
// `diagnostics` also logs every gazer catch event on either role.
void installEarly(bool diagnostics);

// This machine is a guest: the vetoes are on. Set from the net thread each tick.
void setRole(bool guest);

}  // namespace bt_grab
