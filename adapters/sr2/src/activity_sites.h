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

// Phase 1: the race invite memory, the start handshake and the first activities and diversions. The sender scope of the
// message handlers (the milestone 7 rows AF1 to AF4, and AF5's scope) is milestone 9's SS1 and SS2 (session_sites.h).
inline constexpr Site kPhase1[] = {
    {"AF5", Kind::HookEntry, Gate::Now, 0x682770, "51 56 8B 74 24 0C", Thunk::RememberRaceInviter, ""},
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

// Phase 2: the remaining Mayhem, Fraud and Trail Blazing rows, Crowd Control, FUZZ, Snatch and Trafficking targeting and
// credit, Chop Shop.
inline constexpr Site kPhase2[] = {
    {"AV2", Kind::RetargetCall, Gate::Now, 0x64ED12, "E8 29 72 31 00", Thunk::EachRemote965F40, ""},
    {"AV3", Kind::RetargetCall, Gate::Now, 0x64F078, "E8 C3 6E 31 00", Thunk::EachRemote965F40, ""},
    {"AV8", Kind::RetargetCall, Gate::Now, 0x668091, "E8 3A FB 36 00", Thunk::MatchRemoteRememberEsi, ""},
    {"AV9", Kind::RetargetCall, Gate::Now, 0x66811C, "E8 1F C6 1A 00", Thunk::SendToLastMatched, ""},
    {"AV10", Kind::RetargetCall, Gate::Now, 0x66CC39, "E8 92 AF 36 00", Thunk::MatchRemoteEbx, ""},
    {"AV11", Kind::RetargetCall, Gate::Now, 0x66CF0D, "E8 BE AC 36 00", Thunk::MatchRemoteEsi, ""},
    {"AV12", Kind::RetargetCall, Gate::Now, 0x58FDD3, "E8 C8 84 3D 00", Thunk::EachRemote9682A0, ""},
    {"AV13", Kind::RetargetCall, Gate::Now, 0x58FDDB, "E8 30 82 3D 00", Thunk::EachRemote968010, ""},
    {"AV14", Kind::RetargetCall, Gate::Now, 0x5900BB, "E8 50 7F 3D 00", Thunk::EachRemote968010, ""},
    {"AV15", Kind::RetargetCall, Gate::Now, 0x5900C3, "E8 D8 81 3D 00", Thunk::EachRemote9682A0, ""},
    {"AC15", Kind::RetargetCall, Gate::Now, 0x5B8E6B, "E8 60 ED 41 00", Thunk::NearestRemoteEdi14, ""},
    {"AC16", Kind::RetargetCall, Gate::Now, 0x5B9926, "E8 A5 E2 41 00", Thunk::NearestRemoteStack1c14, ""},
    {"AC17", Kind::RetargetCall, Gate::Now, 0x5BDA7C, "E8 4F A1 41 00", Thunk::NearestRemoteEbx, ""},
    {"AC18", Kind::RetargetCall, Gate::Now, 0x5BDAB0, "E8 1B A1 41 00", Thunk::NearestRemoteEbx, ""},
    {"AC19", Kind::RetargetCall, Gate::Now, 0x5C3E39, "E8 92 3D 41 00", Thunk::NearestRemoteEsi14, ""},
    {"AC20", Kind::RetargetCall, Gate::Now, 0x5CAD7D, "E8 4E CE 40 00", Thunk::NearestRemoteEbx, ""},
    {"AC21", Kind::RetargetCall, Gate::Now, 0x5CADA7, "E8 24 CE 40 00", Thunk::NearestRemoteEbx, ""},
    {"AC22", Kind::RetargetCall, Gate::Now, 0x5CAE76, "E8 55 CD 40 00", Thunk::NearestRemoteEbx, ""},
    {"AC23", Kind::RetargetCall, Gate::Now, 0x5CAEA0, "E8 2B CD 40 00", Thunk::NearestRemoteEbx, ""},
    {"AC24", Kind::RetargetCall, Gate::Now, 0x5CB551, "E8 7A C6 40 00", Thunk::NearestRemoteEdi, ""},
    {"AC25", Kind::RetargetCall, Gate::Now, 0x5CB57B, "E8 50 C6 40 00", Thunk::NearestRemoteEdi, ""},
    {"AC26", Kind::RetargetCall, Gate::Now, 0x5CB5EF, "E8 DC C5 40 00", Thunk::NearestRemoteEdi, ""},
    {"AC27", Kind::RetargetCall, Gate::Now, 0x5CB619, "E8 B2 C5 40 00", Thunk::NearestRemoteEdi, ""},
    {"AC28", Kind::RetargetCall, Gate::Now, 0x5CC58A, "E8 41 B6 40 00", Thunk::NearestRemoteEsi, ""},
    {"AC29", Kind::RetargetCall, Gate::Now, 0x5CC5B6, "E8 15 B6 40 00", Thunk::NearestRemoteEsi, ""},
    {"AC30", Kind::RetargetCall, Gate::Now, 0x5D4711, "E8 BA 34 40 00", Thunk::NearestRemoteEbp, ""},
    {"AC31", Kind::RetargetCall, Gate::Now, 0x5D4749, "E8 82 34 40 00", Thunk::NearestRemoteEbp, ""},
    {"AC32", Kind::RetargetCall, Gate::Now, 0x5D66A6, "E8 25 15 40 00", Thunk::NearestRemoteEbx, ""},
    {"AC33", Kind::RetargetCall, Gate::Now, 0x5D66D2, "E8 F9 14 40 00", Thunk::NearestRemoteEbx, ""},
    {"AC34", Kind::RetargetCall, Gate::Now, 0x5D7DF5, "E8 D6 FD 3F 00", Thunk::NearestRemoteEsi, ""},
    {"AC35", Kind::RetargetCall, Gate::Now, 0x5D7E21, "E8 AA FD 3F 00", Thunk::NearestRemoteEsi, ""},
    {"AC36", Kind::RetargetCall, Gate::Now, 0x65988D, "E8 3E E3 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC37", Kind::RetargetCall, Gate::Now, 0x6598BD, "E8 0E E3 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC38", Kind::RetargetCall, Gate::Now, 0x659995, "E8 36 E2 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC39", Kind::RetargetCall, Gate::Now, 0x6599C5, "E8 06 E2 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC40", Kind::RetargetCall, Gate::Now, 0x659B5F, "E8 6C E0 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC41", Kind::RetargetCall, Gate::Now, 0x659B7B, "E8 50 E0 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC42", Kind::RetargetCall, Gate::Now, 0x659DC1, "E8 0A DE 37 00", Thunk::NearestRemoteEdi, ""},
    {"AC43", Kind::RetargetCall, Gate::Now, 0x659DDD, "E8 EE DD 37 00", Thunk::NearestRemoteEdi, ""},
    {"AC44", Kind::RetargetCall, Gate::Now, 0x657072, "E8 59 0B 38 00", Thunk::NearestRemoteEsi, ""},
    {"AC45", Kind::RetargetCall, Gate::Now, 0x657097, "E8 34 0B 38 00", Thunk::MatchRemoteEsi, ""},
    {"AC46", Kind::RetargetCall, Gate::Now, 0x656D69, "E8 62 0E 38 00", Thunk::MatchRemoteEsi, ""},
    {"AC47", Kind::RetargetCall, Gate::Now, 0x65B0F4, "E8 D7 CA 37 00", Thunk::MatchRemoteEsi, ""},
    {"AC48", Kind::RetargetCall, Gate::Now, 0x65B52E, "E8 9D C6 37 00", Thunk::MatchRemoteEsi, ""},
    {"AC49", Kind::RetargetCall, Gate::Now, 0x65E7D1, "E8 FA 93 37 00", Thunk::MatchRemoteEsi, ""},
    {"AC50", Kind::RetargetCall, Gate::Now, 0x65E04C, "E8 7F 9B 37 00", Thunk::MatchRemoteEdi, ""},
    {"AC51", Kind::RetargetCall, Gate::Now, 0x65FE99, "E8 32 7D 37 00", Thunk::NearestRemoteEsi, ""},
    {"AC52", Kind::RetargetCall, Gate::Now, 0x66697C, "E8 4F 12 37 00", Thunk::MatchRemoteStack18, ""},
    {"AC53", Kind::RetargetCall, Gate::Now, 0x666A2B, "E8 A0 11 37 00", Thunk::MatchRemoteStack14, ""},
    {"AD5", Kind::RetargetCall, Gate::Now, 0x5FF57F, "E8 4C 86 3D 00", Thunk::RemoteInVehicleEbx1c, ""},
    {"AD6", Kind::RetargetCall, Gate::ProbeH, 0x5FE96F, "E8 CC 5D 21 00", Thunk::SendToRemotesInVehicleEsp538, ""},
};

// Phase 3: the Hitman and Chop Shop polish, and the Lua script override loader.
inline constexpr Site kPhase3[] = {
    {"AF9", Kind::RetargetCall, Gate::Now, 0xCDDF72, "E8 79 F3 FF FF", Thunk::ScriptOverridePath, ""},
    {"AD7", Kind::RetargetCall, Gate::Now, 0x63D632, "E8 99 A5 39 00", Thunk::NearestRemoteEdi, ""},
    {"AD8", Kind::RetargetCall, Gate::Now, 0x63D663, "E8 68 A5 39 00", Thunk::NearestRemoteEdi, ""},
    {"AD9", Kind::RetargetCall, Gate::Now, 0x63B3EA, "E8 E1 C7 39 00", Thunk::NearestRemoteEsi, ""},
    {"AD10", Kind::RetargetCall, Gate::Now, 0x63B93F, "E8 8C C2 39 00", Thunk::NearestRemoteEdi, ""},
    {"AD11", Kind::RetargetCall, Gate::Now, 0x5FDF27, "E8 A4 9C 3D 00", Thunk::NearestRemoteEdi14, ""},
};

}  // namespace activity_sites
