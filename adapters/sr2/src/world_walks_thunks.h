#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 17 wave 3 (docs/SR2_NOTES.md, "Remote poses, world walks, garage door and client name (milestone 17 wave 3)"): the code
// behind the rows of world_walks_sites.h. Only installed with 3 or more players and the milestone 9 logic; with fewer than two
// remote humans or a pairing that cannot be trusted every thunk gives the game's own answer.
namespace world_walks_thunks {

// With trace on, the thunks log the poses, the spawn rejections, the bars, the warp and garage decisions and the client name.
void setTrace(bool trace);

// The code behind PD2, PD3, SW1, FB1, AW1, GZ1, CN1 and CN2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace world_walks_thunks
