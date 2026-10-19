#pragma once
#include "partner_sites.h"

// Where milestone 16 W1 changes or reads the exe for the outfit, item and weapon slot natives of every remote
// (docs/SR2_NOTES.md, "Outfits, items and weapon slots (M16 W1)"). Copied from the research table of M16_W1_LUA_OUTFITS.md; the
// bytes are what the exe has there. Each table is a set, written all or nothing and only with 3 or more players, and each set
// works alone. The trace table is written one site at a time with trace=1.
namespace outfit_sync_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// OF5 and OF6 (set A): the look sync after clothes are applied here reaches every remote member, not the default partner.
inline constexpr Site kLookSync[] = {
    {"OF5", Kind::RetargetCall, Gate::Now, 0x53FDAD, "E8 9E BE 2E 00", Thunk::EachRemoteMember82BC50, ""},
    {"OF6", Kind::RetargetCall, Gate::Now, 0x53FF08, "E8 43 BD 2E 00", Thunk::EachRemoteMember82BC50, ""},
};

// OF1 and OF2 (set B-item): customization_item_wear with a player number in its mask dresses that remote only.
inline constexpr Site kItemWear[] = {
    {"OF1", Kind::RetargetCall, Gate::Now, 0xA4E343, "E8 68 18 AF FF", Thunk::TargetedItemWear, ""},
    {"OF2", Kind::RetargetCall, Gate::Now, 0x53FD17, "E8 24 4A 2D 00", Thunk::SendToSyncTarget, ""},
};

// OF3 and OF4 (set B-weapon): inv_weapon_enable_or_disable_all_slots with a player number in its mask reaches that remote only.
inline constexpr Site kWeaponSlots[] = {
    {"OF3", Kind::RetargetCall, Gate::Now, 0xA34AEB, "E8 40 18 D6 FF", Thunk::TargetedWeaponSlots, ""},
    {"OF4", Kind::RetargetCall, Gate::Now, 0x7963A6, "E8 95 E3 07 00", Thunk::SendToSyncTarget, ""},
};

// trace=1 only: the three receivers of what the sets send (act 0xC, act 0xD, hud 0x17).
inline constexpr Site kTraces[] = {
    {"OT1", Kind::HookEntry, Gate::Now, 0x53FB20, "8B 54 24 04 80 3A 00 8D", Thunk::TraceOutfitReceived, ""},
    {"OT2", Kind::HookEntry, Gate::Now, 0x6964C0, "51 A1 50 0B E8 00 33 C4", Thunk::TraceRevertReceived, ""},
    {"OT3", Kind::HookEntry, Gate::Now, 0x7960E0, "8B 44 24 04 0F BE 08 83", Thunk::TraceWeaponSlotsReceived, ""},
};

}  // namespace outfit_sync_sites
