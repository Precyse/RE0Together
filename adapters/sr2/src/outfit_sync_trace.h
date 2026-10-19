#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 W1 (trace=1, read-only; docs/SR2_NOTES.md, "Outfits, items and weapon slots (M16 W1)"): the receivers of what the
// outfit sync sets send, so a log shows on every machine that the clothes, the revert or the weapon slot lock arrived:
//   OT1  the act 0xC receiver 0x53FB20 (kind 1 outfit, 0 item; applies to the local player),
//   OT2  the act 0xD receiver 0x6964C0 (the clothes revert),
//   OT3  the hud 0x17 receiver 0x7960E0 (kind 2: enable or disable all weapon slots).
namespace outfit_sync_trace {

// The code behind OT1, OT2 and OT3; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace outfit_sync_trace
