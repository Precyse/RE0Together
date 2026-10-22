#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 16 wave 2 changes the exe for diversions and activities with 3 or 4 players (docs/SR2_NOTES.md, "Diversions
// and activities (milestone 16 wave 2)"). Copied from the research spec M16_W2_DIVERSIONS.md section 4; the bytes are what the
// exe has there. Each table is one set, written all or nothing; a set whose bytes differ is left out and the others still go in.
namespace diversion_sites {

using partner_sites::Kind;
using partner_sites::Set;
using partner_sites::Site;
using partner_sites::Thunk;

// Streaking: the coat of the flashing diversion is a per-slot value (DVS1 to DVS6).
inline constexpr Site kStreaking[] = {
    {"DVS1", Kind::HookEntry, 0x612430, "8B 44 24 04 80 38 01", Thunk::StreakMessage, ""},
    {"DVS2", Kind::HookEntry, 0x611010, "80 3D 95 78 52 02 00", Thunk::StreakTick, ""},
    {"DVS4", Kind::HookEntry, 0x610FE0, "80 3D 95 78 52 02 00", Thunk::StreakEnd, ""},
    {"DVS5", Kind::HookEntry, 0x610D80, "55 8B EC 83 E4 F8", Thunk::StreakInit, ""},
    {"DVS6", Kind::CallBlock, 0x9E660D, "39 3D 84 B2 40 01 75 0E E8 B6 15 FF FF 50 E8 60 A1 C2 FF 83 C4 04 A1 84 B2 40 01", Thunk::StreakCoatBufferOfBuilt, "EB 14 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
};

// Tow truck: the towed car stays valid while any remote sits in it (DVW1).
inline constexpr Site kTow[] = {
    {"DVW1", Kind::RetargetCall, 0x62D0BE, "E8 0D AB 3A 00", Thunk::AnyRemoteInVehicleEsi, ""},
};

// Insurance Fraud: zones, team hits and tandem bonuses for every remote (DVF1 to DVF15).
inline constexpr Site kFraud[] = {
    {"DVF1", Kind::CallBlock, 0x64A0AD, "E8 1E DB 38 00 3B F0 75 0B 83 3D 7C 79 52 02 00", Thunk::FraudZoneOfEsi, "EB 0F 90 90 90 90 90 90 90 90 90"},
    {"DVF2", Kind::CallBlock, 0x64AC3B, "E8 90 CF 38 00 3B D8 75 0E 85 F6 75 04 33 C0 EB 03 8B 46 10 89 78 28", Thunk::FraudTeamHitEbx, "90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"DVF3", Kind::RetargetCall, 0x64ACF1, "E8 DA CE 38 00", Thunk::FraudTeammate, ""},
    {"DVF4", Kind::RetargetCall, 0x64B1DF, "E8 EC C9 38 00", Thunk::AnyRemoteInFraudZoneEbp, ""},
    {"DVF5", Kind::RetargetCall, 0x64E936, "E8 95 92 38 00", Thunk::AnyRemoteInFraudArea, ""},
    {"DVF7", Kind::RetargetCall, 0x64E9F4, "E8 D7 91 38 00", Thunk::AnyRemoteWithFraudZone, ""},
    {"DVF8", Kind::RetargetCall, 0x64F4EF, "E8 DC 86 38 00", Thunk::NearestRemoteLocal, ""},
    {"DVF9", Kind::RetargetCall, 0x64F5AA, "E8 21 86 38 00", Thunk::NearestRemoteLocal, ""},
    {"DVF10", Kind::RetargetCall, 0x64F6A2, "E8 29 85 38 00", Thunk::FraudTandemRemote, ""},
    {"DVF11", Kind::RetargetCall, 0x64F720, "E8 AB 84 38 00", Thunk::MatchRemoteEsi, ""},
    {"DVF12", Kind::RetargetCall, 0x64FD4D, "E8 7E 7E 38 00", Thunk::MatchRemoteEbp, ""},
    {"DVF13", Kind::RetargetCall, 0x64FEC2, "E8 09 7D 38 00", Thunk::FraudTandemMatchEbp, ""},
    {"DVF14", Kind::RetargetCall, 0x651BCD, "E8 FE 5F 38 00", Thunk::OwnerRemoteEsi, ""},
    {"DVF15", Kind::RetargetCall, 0x651CF0, "E8 DB 5E 38 00", Thunk::OwnerRemoteEsi, ""},
};

// Snatch: setup, vehicles, proximity, delivery and carry counts for every remote (DVP2 to DVP9e; DVP9a is AC46 of activity_sites.h).
inline constexpr Site kSnatch[] = {
    {"DVP2", Kind::RetargetCall, 0x656B0B, "E8 C0 10 38 00", Thunk::NearestRemoteToEsi8C, ""},
    {"DVP3", Kind::JumpOut, 0x656C20, "85 DB 74 1F 6A 03 8B CB E8 E3 76 38 00 83 C4 04 6A 01 53 E8 18 97 38 00 6A 00 53 E8 C0 80 38 00 83 C4 08", Thunk::EachRemoteSnatchSetup, ""},
    {"DVP4a", Kind::RetargetCall, 0x65D99A, "E8 31 A2 37 00", Thunk::AnyRemoteInVehicle, ""},
    {"DVP4b", Kind::JumpOut, 0x65DA1E, "D9 E8 6A 01 6A 01 51 83 C9 FF D9 1C 24 8B D6 E8 2E D1 44 00 83 C4 0C", Thunk::EachRemoteVehicleAAAB60, ""},
    {"DVP5a", Kind::CallBlock, 0x65A142, "83 7C 24 24 00", Thunk::NearestCachedPartnerEcx1c, ""},
    {"DVP5b", Kind::CallBlock, 0x65A348, "8B 6C 24 24 85 ED", Thunk::NearestCachedPartnerEbpAtEax, "90"},
    {"DVP6", Kind::RetargetCall, 0x65A924, "E8 A7 D2 37 00", Thunk::NearestRemoteToHoHome, ""},
    {"DVP8", Kind::JumpOut, 0x65AEE9, "33 FF 8D 5D 1C 8B 74 BC 18 85 F6 74 4F 83 BE 74 0D 00 00 00 74 08 D9 05 90 1C DD 00 EB 06 D9 05 5C 73 E8 00 8B 03 D9 5C 24 10 F6 80 10 01 00 00 08 75 29 D9 44 24 10 83 C0 14 8D 4E 14 DD 5C 24 10 E8 F1 9A DA FF DD 44 24 10 DC C8 DE D9 DF E0 F6 C4 01 75 07 56 55 E8 3B C9 FF FF 83 C7 01 83 C3 04 83 FF 02 7C 9E", Thunk::SnatchDeliveryEachPlayer, ""},
    {"DVP9b", Kind::CallBlock, 0x65B805, "E8 C6 C3 37 00 8B 4C 24 10 3B C8 75 0A C7 83 5C 04 00 00 01 00 00 00", Thunk::SnatchCarrySet, "90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"DVP9c", Kind::CallBlock, 0x657C55, "8B B4 24 8C 00 00 00 3B F0 8B BC 24 88 00 00 00 75 1E 29 BD 5C 04 00 00 8B 85 5C 04 00 00 33 C9 85 C0 0F 9E C1 83 E9 01 23 C1 89 85 5C 04 00 00", Thunk::SnatchCarryDelivered, "90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"DVP9d", Kind::CallBlock, 0x65B395, "3B F0 75 0A C7 83 5C 04 00 00 00 00 00 00", Thunk::SnatchCarryClearOf, "90 90 90 90 90 90 90 90 90"},
    {"DVP9e", Kind::CallBlock, 0x65BC39, "C7 87 5C 04 00 00 00 00 00 00", Thunk::SnatchCarryClearAll, "90 90 90 90 90"},
};

// Trafficking: followers, weapons, end warp and vehicle stops for every remote, the dealer follows any remote (DVK1 to DVK6).
inline constexpr Site kTrafficking[] = {
    {"DVK1", Kind::RetargetCall, 0x660F1F, "E8 AC 6C 37 00", Thunk::RemoteLeadingDealer, ""},
    {"DVK2", Kind::RetargetCall, 0x65FE03, "E8 58 F6 32 00", Thunk::EachRemoteDismissFollowers, ""},
    {"DVK3", Kind::RetargetCall, 0x65FE21, "E8 8A 73 00 00", Thunk::EachRemoteTraffickingArm, ""},
    {"DVK4", Kind::RetargetCall, 0x65FBBE, "E8 2D 76 00 00", Thunk::EachRemoteTraffickingDisarm, ""},
    {"DVK5", Kind::RetargetCall, 0x666CAA, "E8 D1 47 37 00", Thunk::TeleportRemotesForActivity, ""},
    {"DVK6", Kind::JumpOut, 0x666D4D, "E8 7E 0E 37 00 85 C0 74 2A 8B 88 74 0D 00 00 85 C9 74 20 3B 0E 74 1C D9 E8 6A 01 6A 01 51 D9 1C 24 E8 7D BB 47 00 83 C9 FF 8B D0 E8 E3 3D 44 00 83 C4 0C", Thunk::EachRemoteStopVehicle, ""},
};

// Mayhem: weapons, credit, destruction identity and kill stats for every remote (DVM1 to DVM11).
inline constexpr Site kMayhem[] = {
    {"DVM1", Kind::RetargetCall, 0x66B0B7, "E8 F4 FA FF FF", Thunk::EachRemoteMayhemArm, ""},
    {"DVM2", Kind::RetargetCall, 0x66B703, "E8 B8 FD FF FF", Thunk::EachRemoteMayhemDisarm, ""},
    {"DVM3", Kind::JumpOut, 0x66862C, "6A 00 E8 9D F5 36 00 D9 44 24 10 50 51 D9 1C 24 6A 00 E8 8D F5 36 00 8B 70 44 E8 F5 FB FF FF 83 C4 10", Thunk::EachRemoteMayhemCredit, ""},
    {"DVM4", Kind::RetargetCall, 0x66C6A9, "E8 22 B5 36 00", Thunk::MatchRemoteEdi30, ""},
    {"DVM5", Kind::RetargetCall, 0x66EEBA, "E8 61 9D 35 00", Thunk::EachRemote9C8C20, ""},
    {"DVM8", Kind::RetargetCall, 0x66C3D8, "E8 F3 B7 36 00", Thunk::NearestRemoteLocal, ""},
    {"DVM9", Kind::RetargetCall, 0x66CAAF, "E8 1C B1 36 00", Thunk::MatchRemoteEbp, ""},
    {"DVM10", Kind::RetargetCall, 0x66CDC1, "E8 0A AE 36 00", Thunk::MatchRemoteEbx, ""},
    {"DVM11", Kind::RetargetCall, 0x66D091, "E8 3A AB 36 00", Thunk::MatchRemoteEsi, ""},
};

// The district objective activity: any remote standing in the district counts (DVO1 to DVO6).
inline constexpr Site kDistrict[] = {
    {"DVO1", Kind::RetargetCall, 0x562A39, "E8 92 51 47 00", Thunk::RemoteInZoneEdi, ""},
    {"DVO2", Kind::RetargetCall, 0x5642E0, "E8 EB 38 47 00", Thunk::RemoteInZoneAtEsi, ""},
    {"DVO3", Kind::RetargetCall, 0x5647B3, "E8 18 34 47 00", Thunk::RemoteInZoneAtEsi, ""},
    {"DVO4", Kind::RetargetCall, 0x5653BF, "E8 0C 28 47 00", Thunk::RemoteInZoneAtEbp, ""},
    {"DVO5", Kind::RetargetCall, 0x5664A5, "E8 26 17 47 00", Thunk::RemoteInZoneE9E3F4, ""},
    {"DVO6", Kind::RetargetCall, 0x564F67, "E8 64 2C 47 00", Thunk::NearestRemoteEsi, ""},
};

// Phase B: DVP1, DVP7, DVM6, DVK7.
inline constexpr Site kPhaseB[] = {
    {"DVP1", Kind::HookEntry, 0x655F70, "81 EC C0 00 00 00", Thunk::SnatchSpawnPerRemote, ""},
    {"DVP7", Kind::HookEntry, 0x65ABA0, "83 EC 40 8B 0D D4 03 17 02", Thunk::SnatchWaypointPerRemote, ""},
    {"DVM6", Kind::JumpOut, 0x66BFB7, "33 FF 8D 6C 24 1C 8B 74 BC 18 85 F6 74 7C 8B 86 00 0F 00 00 85 C0 75 36 8B 4D 00 85 C9 74 06 8B 81 00 0F 00 00 50 8D 56 14 52 E8 0A 5A E4 FF 83 C4 08 83 F8 FF 74 0E 69 C0 A8 00 00 00 03 05 94 6C 52 02 75 09 C6 83 48 05 00 00 00 EB 3C 8B 8B 44 05 00 00 8B 40 54 6B C9 64 8B 53 04 3B 44 0A 50 0F 94 C0 20 83 48 05 00 00 80 BB 49 05 00 00 00 74 17 84 C0 75 0B 6A 01 56 53 E8 69 FD FF FF EB 08 53 8B CE E8 5F FC FF FF 83 C7 01 83 ED 04 83 FF 02 0F 82 6D FF FF FF", Thunk::MayhemDistrictAllPlayers, ""},
    {"DVK7", Kind::JumpOut, 0x666ECE, "5F 5E 5D 5B 83 C4 34 C3", Thunk::TraffickingExtraSeats, ""},
};

inline constexpr Set kSets[] = {
    {"streaking", kStreaking}, {"tow", kTow},           {"fraud", kFraud},       {"snatch", kSnatch},
    {"trafficking", kTrafficking}, {"mayhem", kMayhem}, {"district", kDistrict}, {"phase B", kPhaseB},
};

}  // namespace diversion_sites
