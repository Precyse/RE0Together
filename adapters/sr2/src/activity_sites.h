#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 7 changes the exe (docs/SR2_NOTES.md, "Activities"). Copied from the research table
// m7_patch_sites.tsv; the bytes are what the exe has there. The sites are applied in phases, each all or nothing.
namespace activity_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// Phase 1: the sender scope, the start handshake and the first activities and diversions.
inline constexpr Site kPhase1[] = {
    {"AF1", Kind::HookEntry, Gate::Now, 0x8CA340, "8B 4C 24 04 0F BE 01", Thunk::ScopeActPacket, ""},
    {"AF2", Kind::HookEntry, Gate::Now, 0x8CA360, "8B 4C 24 04 0F BE 01", Thunk::ScopeMiscPacket, ""},
    {"AF3", Kind::HookEntry, Gate::Now, 0x6A8AD0, "A1 14 8C 52 02", Thunk::ScopeMissionRequest, ""},
    {"AF4", Kind::HookEntry, Gate::Now, 0x68FD40, "8B 0D 14 8C 52 02", Thunk::ScopeGameplayChanged, ""},
    {"AF5", Kind::HookEntry, Gate::Now, 0x682770, "51 56 8B 74 24 0C", Thunk::ScopeRaceRequest, ""},
    {"AF6", Kind::RetargetCall, Gate::Now, 0x6A8B46, "E8 15 FE FF FF", Thunk::DeclineUnlessAllDeclined, ""},
    {"AF7", Kind::RetargetCall, Gate::Now, 0x5E38AE, "E8 BD 0D 23 00", Thunk::SendToEachRemoteMember, ""},
    {"AF8", Kind::RetargetCall, Gate::Now, 0x53E3DA, "E8 A1 D0 49 00", Thunk::TeleportRemotesForActivity, ""},
    {"AV1", Kind::CallBlock, Gate::Now, 0x64ED6A, "8B 44 24 14 85 C0 74 09 80 B8 95 05 00 00 00 75 2E", Thunk::AnyRemoteRagdolling, "84 C0 0F 85 32 00 00 00 90 90 90 90"},
    {"AV4", Kind::RetargetCall, Gate::Now, 0x6682AC, "E8 1F F9 36 00", Thunk::MatchRemoteEsp560, ""},
    {"AV5", Kind::RetargetCall, Gate::Now, 0x66833C, "E8 FF C3 1A 00", Thunk::SendToHumanOwnerEsp568, ""},
    {"AV6", Kind::RetargetCall, Gate::Now, 0x66849E, "E8 2D F7 36 00", Thunk::MatchRemoteEbp, ""},
    {"AV7", Kind::RetargetCall, Gate::Now, 0x668562, "E8 D9 C1 1A 00", Thunk::SendToHumanOwnerEbp, ""},
    {"AV16", Kind::HookEntry, Gate::ProbeE, 0x5E7F10, "8B 4C 24 04 8A 01 3C 05", Thunk::DerbyParticipantsOnly, ""},
    {"AC1", Kind::RetargetCall, Gate::Now, 0x5AE71D, "E8 AE 94 42 00", Thunk::MatchRemoteEsi, ""},
    {"AC2", Kind::RetargetCall, Gate::Now, 0x5AE820, "E8 AB 93 42 00", Thunk::MatchRemoteEbx, ""},
    {"AC3", Kind::RetargetCall, Gate::Now, 0x5AE945, "E8 86 92 42 00", Thunk::MatchRemoteEsi, ""},
    {"AC4", Kind::RetargetCall, Gate::Now, 0x5AEBAA, "E8 21 90 42 00", Thunk::MatchRemoteEsi, ""},
    {"AC5", Kind::RetargetCall, Gate::Now, 0x5AE9A3, "E8 28 92 42 00", Thunk::NearestRemoteEsi, ""},
    {"AC6", Kind::RetargetCall, Gate::Now, 0x5AE9CB, "E8 00 92 42 00", Thunk::NearestRemoteEsi, ""},
    {"AC7", Kind::RetargetCall, Gate::Now, 0x5A8761, "E8 6A F4 42 00", Thunk::NearestRemoteEsi, ""},
    {"AC8", Kind::RetargetCall, Gate::Now, 0x5A878B, "E8 40 F4 42 00", Thunk::NearestRemoteEsi, ""},
    {"AC9", Kind::RetargetCall, Gate::Now, 0x5A880B, "E8 C0 F3 42 00", Thunk::NearestRemoteEsi, ""},
    {"AC10", Kind::RetargetCall, Gate::Now, 0x5A8835, "E8 96 F3 42 00", Thunk::NearestRemoteEsi, ""},
    {"AC11", Kind::RetargetCall, Gate::Now, 0x5ABACB, "E8 00 C1 42 00", Thunk::NearestRemoteToCelebEbx, ""},
    {"AC12", Kind::RetargetCall, Gate::Now, 0x5BBE7E, "E8 FD E4 30 00", Thunk::RecordZombieRequester, ""},
    {"AC13", Kind::RetargetCall, Gate::Now, 0x5B7448, "E8 83 07 42 00", Thunk::ZombiePartner, ""},
    {"AC14", Kind::HookEntry, Gate::Now, 0x5BBD40, "81 EC 04 02 00 00", Thunk::ZombieAnswerGate, ""},
    {"AD1", Kind::RetargetCall, Gate::Now, 0x63E6EE, "E8 DD 94 39 00", Thunk::MatchRemoteEcx, ""},
    {"AD2", Kind::RetargetCall, Gate::Now, 0x68FD22, "E8 19 4A 18 00", Thunk::SendToScopedMember, ""},
    {"AD3", Kind::HookEntry, Gate::Now, 0x6418D0, "53 56 57 8B 3D D4 03 17 02", Thunk::BlockCatAndMouse, ""},
    {"AD4", Kind::HookEntry, Gate::Now, 0x645A00, "53 56 57 8B 3D D4 03 17 02", Thunk::BlockDeathTag, ""},
};

}  // namespace activity_sites
