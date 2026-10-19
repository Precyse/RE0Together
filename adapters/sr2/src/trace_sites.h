#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 9 reads the exe for the traces of the players area (trace=1 only; docs/SR2_NOTES.md, "Player traces").
// Copied from the research table m9_players_sites.tsv.
namespace trace_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

inline constexpr Site kPlayers[] = {
    {"TF1", Kind::HookEntry, Gate::Now, 0x9C1B90, "55 8B EC 83 E4 F8 81 EC", Thunk::TraceShot, ""},
    {"TF2", Kind::PointerSwap, Gate::Now, 0xE2B444, "D0 33 86 00", Thunk::TracePlayerPlayback, ""},
    {"TL2", Kind::HookEntry, Gate::Now, 0x9E9EB0, "51 A1 38 C4 E8 00", Thunk::TraceCoopBuffers, ""},
};

// The pool census (milestone 11 B6, milestone 12; docs/SR2_NOTES.md, "Pool census"): the NPC human setup, which counts a
// create that returned nothing (NC1), the chunk node loader, which takes groups and elements outside the group create (NC2),
// and the vehicle class create, whose failed attempts are named per net id (VT1).
inline constexpr Site kCensus[] = {
    {"NC1", Kind::HookEntry, Gate::Now, 0x9D0DE0, "51 53 8A 5C 24 0C 84 DB", Thunk::TraceNpcSetup, ""},
    {"NC2", Kind::HookEntry, Gate::Now, 0x9140C0, "81 EC 4C 02 00 00 A1 50 0B E8", Thunk::TraceChunkNodes, ""},
    {"VT1", Kind::PointerSwap, Gate::Now, 0xE2BA78, "30 3F 87 00", Thunk::TraceRemoteVehicle, ""},
};

// The cutscene end (milestone 13; docs/SR2_NOTES.md, "Cutscenes"): the stores that start the done wait (CE3), end it by the
// game's own timer (CE4) and finish the cutscene (CE5). Each thunk makes the store it replaces and logs.
inline constexpr Site kCutsceneTrace[] = {
    {"CE3", Kind::CallBlock, Gate::Now, 0x6C64CB, "C6 05 15 7D 52 02 01", Thunk::CutsceneDoneWait, "90 90"},
    {"CE4", Kind::CallBlock, Gate::Now, 0x6C68E9, "C6 05 20 7D 52 02 01", Thunk::CutsceneDoneTimeout, "90 90"},
    {"CE5", Kind::CallBlock, Gate::Now, 0x6C69AF, "C6 05 15 7D 52 02 00", Thunk::CutsceneFinished, "90 90"},
};

// Script errors and fades (milestone 13; docs/SR2_NOTES.md, "Script errors and fades"): the thread runner's lua_resume (CE1),
// the two load error alerts (CE2), the script fade natives (FD1, FD2) and the fade tick (FD3).
inline constexpr Site kScriptTrace[] = {
    {"CE1", Kind::RetargetCall, Gate::Now, 0xCE0645, "E8 E6 A5 FF FF", Thunk::LuaThreadResume, ""},
    {"CE2a", Kind::RetargetCall, Gate::Now, 0xCD97E5, "E8 76 F8 FF FF", Thunk::LuaLoadError, ""},
    {"CE2b", Kind::RetargetCall, Gate::Now, 0xCD99FF, "E8 5C F6 FF FF", Thunk::LuaLoadError, ""},
    {"FD1", Kind::HookEntry, Gate::Now, 0xA2E280, "55 8B EC 83 E4 F8 83 EC 0C 53 56 57", Thunk::TraceFadeIn, ""},
    {"FD2", Kind::HookEntry, Gate::Now, 0xA2E390, "55 8B EC 83 E4 F8 83 EC 1C 53 56 8B", Thunk::TraceFadeOut, ""},
    {"FD3", Kind::HookEntry, Gate::Now, 0x4D1790, "83 EC 10 83 3D 9C 6D 52 02 00", Thunk::TraceFadeTick, ""},
};

}  // namespace trace_sites
