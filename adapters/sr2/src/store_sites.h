#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 16 wave 2 changes the exe for the stores, the garage and the activity level menu (docs/SR2_NOTES.md, "Store,
// clothing and activity level menus (milestone 16 wave 2)"). Copied from the research table of M16_W2_STORE.md; the bytes are what
// the exe has there. Each table is one set, written all or nothing and only with 3 or more players and the milestone 9 logic. A check
// row (Bytes, no tail) only confirms the bytes a stub jumps back to. The look sync after a wear and the targeted item wear of the
// same research are the W1 sets OF1, OF2, OF5 and OF6 (outfit_sync_sites.h).
namespace store_sites {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// SH1 to SH3: the store entry and exit and the host's garage re-hide hide and show every remote player human, where the game's block
// did it for the default partner.
inline constexpr Site kHide[] = {
    {"SH1", Kind::JumpOut, 0x74021C,
     "E8 AF 79 29 00 85 C0 74 1E E8 A6 79 29 00 83 88 80 00 00 00 10 05 80 00 00 00 E8 95 79 29 00 05 80 00 00 00 83 08 04",
     Thunk::HideRemotesInStore, ""},
    {"SH1r", Kind::Bytes, 0x740243, "3B DE 74 27", Thunk::None, ""},
    {"SH2", Kind::JumpOut, 0x7407BA,
     "E8 11 74 29 00 85 C0 74 1E E8 08 74 29 00 83 A0 80 00 00 00 EF 05 80 00 00 00 E8 F7 73 29 00 05 80 00 00 00 83 20 FB",
     Thunk::ShowRemotesAfterStore, ""},
    {"SH2r", Kind::Bytes, 0x7407E1, "39 1D F4 7F 52 02", Thunk::None, ""},
    {"SH3", Kind::JumpOut, 0xA6D015,
     "E8 B6 AB F6 FF 85 C0 74 1E E8 AD AB F6 FF 83 88 80 00 00 00 10 05 80 00 00 00 E8 9C AB F6 FF 05 80 00 00 00 83 08 04",
     Thunk::HideRemotesInGarage, ""},
    {"SH3r", Kind::Bytes, 0xA6D03C, "5E C3", Thunk::None, ""},
};

// GB1: the garage will not store a vehicle while a remote stands in its box (0xBE9890 tested the default partner's position only).
inline constexpr Site kGarage[] = {
    {"GB1", Kind::RetargetCall, 0xA6AA28, "E8 63 EE 17 00", Thunk::AnyRemoteInVehicleBox, ""},
};

// LM1 to LM4: the level menu opens on the machine of the activity's owner only, and a guest's answer goes to the host only.
inline constexpr Site kActivityLevel[] = {
    {"LM1", Kind::RetargetCall, 0x75BC5C, "E8 6F BF 27 00", Thunk::ActivityOwnerIfRemote, ""},
    {"LM2", Kind::RetargetCall, 0x75BCBB, "E8 80 8A 0B 00", Thunk::SendToActivityOwner, ""},
    {"LM3", Kind::RetargetCall, 0x75B671, "E8 CA 90 0B 00", Thunk::SendAnswerToHost, ""},
    {"LM4", Kind::RetargetCall, 0x75B744, "E8 F7 8F 0B 00", Thunk::SendAnswerToHost, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"hide", kHide},
    {"garage", kGarage},
    {"activity level", kActivityLevel},
};

}  // namespace store_sites
