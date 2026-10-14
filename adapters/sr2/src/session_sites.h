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

// A look is applied to the sender's own human.
inline constexpr Site kLooks[] = {
    {"LK1", Kind::HookEntry, Gate::Now, 0x82BAA0, "8B 0D F4 64 A4 02 83 EC", Thunk::LookMorphFromSender, ""},
    {"LK2", Kind::HookEntry, Gate::Now, 0x82BD00, "8B 0D 44 65 A4 02 83 EC", Thunk::LookComponentsFromSender, ""},
};

// The host admits several joiners one after the other (N1).
inline constexpr Site kAdmission[] = {
    {"N1a", Kind::HookEntry, Gate::Now, 0x831CA0, "8B 4C 24 08 8B 01", Thunk::AdmissionGate, ""},
    {"N1b", Kind::RetargetCall, Gate::Now, 0x8CB2CE, "E8 DD 76 F6 FF", Thunk::ClearAdmissionIfLeaver, ""},
    {"N1c", Kind::HookEntry, Gate::Now, 0x8CB490, "81 EC 94 04 00 00", Thunk::AdmissionPump, ""},
};

}  // namespace session_sites

