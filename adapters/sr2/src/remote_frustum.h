#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 W3 (docs/SR2_NOTES.md, "Remote views"): ambient spawns and despawns respect every remote's view.
//   W3b   after the game builds the partner's frustum (0x523EB0) the frustum of every remote is built the same way and kept,
//         and the partner's goes back into 0x277CE00 last, so the game's own readers see what they always saw,
//   W3d-h the five tests against the partner frustum (0xA020C0) ask the frustum of every remote.
namespace remote_frustum {

// The code behind W3b and W3d-h; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace remote_frustum
