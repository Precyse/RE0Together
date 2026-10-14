#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 9 changes the exe for the session layer (docs/SR2_NOTES.md, "Sender scope" and the sections after it).
// Copied from the research tables m9_players_sites.tsv and m9_net_sites.tsv; the bytes are what the exe has there. Each
// table is a phase, patched all or nothing, and only with 3 or more players.
namespace session_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// The message handlers answer for their sender.
inline constexpr Site kScope[] = {
    {"SS1", Kind::HookEntry, Gate::Now, 0x8138F0, "51 8B 44 24 08 8A 10 80", Thunk::ScopeDispatch, ""},
    {"SS2", Kind::HookEntry, Gate::Now, 0x813390, "83 EC 10 53 56 8B 74 24", Thunk::ScopeInOrder, ""},
};

}  // namespace session_sites
