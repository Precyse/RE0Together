#pragma once
#include <span>

#include "partner_sites.h"
#include "sr2_addresses.h"

// Where milestone 17 wave 3 changes the exe for FUZZ with 3 or 4 players (docs/SR2_NOTES.md, "FUZZ (milestone 17 wave 3)"). Copied
// from the research spec M17_W3_FUZZ.md section 3; the bytes are what the exe has there. Each table is one set, written all or
// nothing; a set whose bytes differ is left out and the others still go in.
namespace fuzz_sites {

using partner_sites::Kind;
using partner_sites::Set;
using partner_sites::Site;
using partner_sites::Thunk;

// The level start and end: every remote loses its followers and gets the FUZZ mode byte (FZ1 to FZ3).
inline constexpr Site kLevel[] = {
    {"FZ1", Kind::RetargetCall, 0x5BEBCE, "E8 8D 08 3D 00", Thunk::EachRemoteDismissFollowers, ""},
    {"FZ2", Kind::RetargetCall, 0x5BEBE4, "E8 47 9D 3A 00", Thunk::EachRemoteFuzzMode, ""},
    {"FZ3", Kind::RetargetCall, 0x5BFE7A, "E8 B1 8A 3A 00", Thunk::EachRemoteFuzzMode, ""},
};

// The crime weapons: every remote gets, swaps and loses the FUZZ weapons (FZ4 to FZ7, FZ9 to FZ11).
inline constexpr Site kWeapons[] = {
    {"FZ4", Kind::RetargetCall, 0x5C0799, "E8 52 15 5B 00", Thunk::EachRemoteGiveWeapon, ""},
    {"FZ5", Kind::RetargetCall, 0x5C12CB, "E8 00 11 5B 00", Thunk::EachRemoteRemoveWeapon, ""},
    {"FZ6", Kind::RetargetCall, 0x5C158F, "E8 3C 0E 5B 00", Thunk::EachRemoteRemoveWeapon, ""},
    {"FZ7", Kind::RetargetCall, 0x5C15B3, "E8 38 07 5B 00", Thunk::EachRemoteGiveWeapon, ""},
    {"FZ9", Kind::RetargetCall, 0x5C1E7A, "E8 71 B3 F7 FF", Thunk::EachRemoteGivePickup, ""},
    {"FZ10", Kind::RetargetCall, 0x5C1FC4, "E8 07 04 5B 00", Thunk::EachRemoteRemoveWeapon, ""},
    {"FZ11", Kind::RetargetCall, 0x5C1FE3, "E8 08 B2 F7 FF", Thunk::EachRemoteGivePickup, ""},
};

// The crime area: a player standing in it starts and advances the crime, any remote counts (FZ8).
inline constexpr Site kCrimeArea[] = {
    {"FZ8", Kind::RetargetCall, 0x5C170B, "E8 C0 64 41 00", Thunk::RemoteInCrimeDistrictEbx, ""},
};

// The cameraman follows the nearest player and films any player who fights (FZ12, FZ13).
inline constexpr Site kCameraman[] = {
    {"FZ12", Kind::RetargetCall, 0x5C22A8, "E8 23 59 41 00", Thunk::NearestRemoteEbp14, ""},
    {"FZ13", Kind::RetargetCall, 0x5C2302, "E8 29 E9 39 00", Thunk::AnyRemoteFighting, ""},
};

// The crime NPCs attack a random remote (FZ14, FZ16).
inline constexpr Site kTargets[] = {
    {"FZ14", Kind::RetargetCall, 0x5C39C0, "E8 0B 42 41 00", Thunk::RandomRemoteHuman, ""},
    {"FZ16", Kind::RetargetCall, 0x5C5B9F, "E8 2C 20 41 00", Thunk::RandomRemoteHuman, ""},
};

// A player in the crime's vehicle sets the crime's flag, any remote counts (FZ17).
inline constexpr Site kVehicle[] = {
    {"FZ17", Kind::RetargetCall, 0x5C720D, "E8 9E 1A 3F 00", Thunk::AnyRemoteInVehicleEcx, ""},
};

// The riot: the "near" tests, the spawn points and the rioters that join, for every remote (FZ15, FZ18 to FZ20). FZc1 and FZc2 are
// the two places the FZ20 block jumps to.
inline constexpr Site kRiot[] = {
    {"FZ15", Kind::HookEntry, 0x5C3A60, "83 EC 0C A1 D4 03 17 02", Thunk::FuzzRiotNearAnyRemote, ""},
    {"FZ18", Kind::RetargetCall, 0x5CDADF, "E8 EC A0 40 00", Thunk::NearestRemoteToStack8, ""},
    {"FZ19", Kind::HookEntry, 0x5CE2B0, "83 EC 0C 56 57 8B 3D D4", Thunk::FuzzPointNearAnyRemote, ""},
    {"FZ20", Kind::CallBlock, 0x5CE6B2,
     "8B 44 24 14 85 C0 0F 84 01 01 00 00 83 C0 14 8B CD E8 58 63 E3 FF DC 5C 24 20 DF E0 F6 C4 41 0F 8A E8 00 00 00 8B 44 24 14 D9 46 18 D8 60 18 D9 E1 D9 5C 24 1C D9 44 24 1C D8 1D 2C 52 DD 00 DF E0 F6 C4 05 0F 8A C3 00 00 00",
     Thunk::FuzzRioterNearAnyRemote,
     "84 C0 0F 84 00 01 00 00 EB 3B 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"FZc1", Kind::Bytes, sr2::kFuzzRioterJoins, "D9 EE 6A 00", Thunk::None, ""},
    {"FZc2", Kind::Bytes, sr2::kFuzzRioterSkips, "8B B6 60 11 00 00", Thunk::None, ""},
};

inline constexpr Set kSets[] = {
    {"level", kLevel},       {"weapons", kWeapons}, {"crime area", kCrimeArea}, {"cameraman", kCameraman},
    {"targets", kTargets},   {"vehicle", kVehicle}, {"riot", kRiot},
};

}  // namespace fuzz_sites
