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

// The cutscene handshake waits for every remote (MC1, MC2), and the ready byte's set lives as long as the byte (CS1, CS2).
inline constexpr Site kCutscene[] = {
    {"MC1", Kind::HookEntry, Gate::Now, 0x6BECA0, "83 EC 3C 57 8B 7C 24 44", Thunk::CutsceneAllRemotes, ""},
    {"MC2", Kind::CallBlock, Gate::Now, 0x6C3E9D, "C6 05 1E 7D 52 02 00", Thunk::CutsceneSetsReset, "90 90"},
    {"CS1", Kind::CallBlock, Gate::Now, 0x6C5849, "C6 05 1D 7D 52 02 00", Thunk::CutsceneReadyConsumed, "90 90"},
    {"CS2", Kind::CallBlock, Gate::Now, 0x6C6AB4, "C6 05 1D 7D 52 02 00", Thunk::CutsceneReadyConsumed, "90 90"},
};

// "The partner paused" is a bit per slot, and the game applies the pause only when the byte goes between zero and not zero
// (N4a replaces the store, the jump and the tail jump of the 0x80 handler; N4b the clear in the member left callback).
inline constexpr Site kPause[] = {
    {"N4a", Kind::JumpOut, Gate::Now, 0x8CB12A, "88 0D 2F 93 52 02 7C 05 E9 59 F0 FF FF", Thunk::PauseMessage, ""},
    {"N4b", Kind::JumpOut, Gate::Now, 0x8CB28E, "80 3D 2F 93 52 02 00 74 0C C6 05 2F 93 52 02 00 E8 ED EE FF FF", Thunk::MemberLeft, ""},
};

// A guest runs the leave pass for another guest that leaves, and ends a cutscene only for the host leaving (N2a, N3).
inline constexpr Site kGuestLeave[] = {
    {"N2a", Kind::RetargetCall, Gate::Now, 0x8CB428, "E8 63 B7 DF FF", Thunk::GuestMemberLeft, ""},
};

// The host admits several joiners one after the other (N1).
inline constexpr Site kAdmission[] = {
    {"N1a", Kind::HookEntry, Gate::Now, 0x831CA0, "8B 4C 24 08 8B 01", Thunk::AdmissionGate, ""},
    {"N1b", Kind::RetargetCall, Gate::Now, 0x8CB2CE, "E8 DD 76 F6 FF", Thunk::ClearAdmissionIfLeaver, ""},
    {"N1c", Kind::HookEntry, Gate::Now, 0x8CB490, "81 EC 94 04 00 00", Thunk::AdmissionPump, ""},
};

}  // namespace session_sites

