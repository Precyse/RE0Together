#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 16 wave 1 changes the exe (docs/SR2_NOTES.md, "Kick and session admin (milestone 16 wave 1)"): the kick menu
// names and kicks one chosen remote instead of the default partner, and the host's session end removes every remote. The bytes
// are what the exe has there. Each table is one set, written all or nothing and only with 3 or more players and the milestone 9
// logic.
namespace session_kick_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// KA1 hooks coop_kick_player (the picker; its trampoline is the game's confirm). KA2 to KA4 are the three partner getter calls of
// the confirm builder, the confirm callback and kick_coop_player (0x8CA380): they answer the chosen remote.
inline constexpr Site kKickTarget[] = {
    {"KA1", Kind::HookEntry, Gate::Now, 0x7EF5E0, "81 EC 08 03 00 00 A1 50", Thunk::KickPickerOrConfirm, ""},
    {"KA2", Kind::RetargetCall, Gate::Now, 0x7EF5F6, "E8 85 AD 0D 00", Thunk::KickTargetNamed, ""},
    {"KA3", Kind::RetargetCall, Gate::Now, 0x7EF534, "E8 47 AE 0D 00", Thunk::KickTargetTaken, ""},
    {"KA4", Kind::RetargetCall, Gate::Now, 0x7EEB18, "E8 63 B8 0D 00", Thunk::KickTargetTaken, ""},
};

// KA5: the sign-in loss on the host removed the partner only; the call of the member removal 0x80F9B0 removes every remote.
inline constexpr Site kSessionEnd[] = {
    {"KA5", Kind::RetargetCall, Gate::Now, 0x69A1D6, "E8 D5 57 17 00", Thunk::RemoveEveryRemoteMember, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"kick target", kKickTarget},
    {"session end", kSessionEnd},
};

}  // namespace session_kick_sites
