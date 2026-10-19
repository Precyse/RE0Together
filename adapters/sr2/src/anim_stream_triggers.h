#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 W1 phase E (anim_stream.h): the activity trigger actions Set_Player_Character and Restore_Player_Character keep the
// partner's preset in one global, [0x2527B68], so a third player entering a trigger released the second player's preset. Each member
// has its own entry here and the global always holds the default partner's, as the game's tick and join replay expect.
//   TS1  misc sub 0x16 (the remote entered or left a trigger) runs with the sender's entry in the global,
//   TS2  the trigger tick releases the entries of members that left and puts the default partner's entry in the global,
//   TS3  the trigger cleanup gets one more pass of the game's own loop body for each other member's entry,
//   TS4  the trigger system reset empties the table.
namespace anim_stream_triggers {

// trace=1 logs the preset changes and releases.
void setTrace(bool trace);

// The code behind TS1 to TS4; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace anim_stream_triggers
