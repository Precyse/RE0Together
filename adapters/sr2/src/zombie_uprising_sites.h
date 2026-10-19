#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 17 wave 3 changes the exe for Zombie Uprising with 3 or 4 players (docs/SR2_NOTES.md, "Zombie Uprising for 3 to 4
// players (milestone 17 wave 3)"). Copied from the research table of M17_W3_ZOMBIE.md; the bytes are what the exe has there. Each table
// is one set, written all or nothing and only with the activity logic and 3 or more players. A check row (Bytes, no tail) only confirms
// the bytes a block jumps back to or a function the stubs call.
namespace zombie_uprising_sites {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// ZU1, ZU2: the wave start gives every remote its own followers off, no regeneration, vehicle exit and start spot, and the end gives each
// the restore. The shipped blocks do it for the default partner only. ZUc4 to ZUc8 confirm the workers and the offset the stubs use.
inline constexpr Site kStartEnd[] = {
    {"ZU1", Kind::CallBlock, 0x5BA8D7, "E8 F4 D2 41 00 85 C0 0F 84 84 00 00 00", Thunk::ZombieStartEachRemote, "E9 87 00 00 00 90 90 90"},
    {"ZUc1", Kind::Bytes, 0x5BA968, "8B 0D D4 03 17 02 53 E8", Thunk::None, ""},
    {"ZU2", Kind::CallBlock, 0x5BAE16, "E8 B5 CD 41 00 85 C0 74 1F", Thunk::ZombieEndEachRemote, "EB 21"},
    {"ZUc2", Kind::Bytes, 0x5BAE3E, "D9 43 2C C6 05 A2 83 E8", Thunk::None, ""},
    {"ZUc4", Kind::Bytes, 0x9DE310, "81 EC 20 05 00 00 A1 50", Thunk::None, ""},
    {"ZUc5", Kind::Bytes, 0x9694A0, "81 EC 28 05 00 00 A1 50", Thunk::None, ""},
    {"ZUc6", Kind::Bytes, 0xB076C0, "83 EC 24 85 FF 53 8B 5C", Thunk::None, ""},
    {"ZUc7", Kind::Bytes, 0x9DB480, "55 8B EC 83 E4 F8 81 EC", Thunk::None, ""},
    {"ZUc8", Kind::Bytes, 0xE5BF40, "00 00 00 00 00 00 F0 3F", Thunk::None, ""},
};

// ZU3: the wave bonus averages the health of every remote with the local player's (the shipped block added the default partner's).
inline constexpr Site kWaveBonus[] = {
    {"ZU3", Kind::CallBlock, 0x5B9C1E,
     "8B C8 E8 8B CB E4 FF D8 44 24 10 D9 5C 24 10 D9 44 24 10 DC 0D 70 BF E5 00 D9 5C 24 10", Thunk::ZombieWaveHealthAverage, "EB 16"},
    {"ZUc3", Kind::Bytes, 0x5B9C3B, "D9 44 24 10 DC 0D 98 BF", Thunk::None, ""},
    {"ZUc9", Kind::Bytes, 0x4067B0, "51 8B 81 20 11 00 00 85", Thunk::None, ""},
};

// ZU4, ZU5: the arena fail and warning zones test every remote's position (0xBE9890 tested the default partner's), through the garage
// box thunk of GB1 (store_sites.h).
inline constexpr Site kTriggerZones[] = {
    {"ZU4", Kind::RetargetCall, 0x5B9D5E, "E8 2D FB 62 00", Thunk::AnyRemoteInVehicleBox, ""},
    {"ZU5", Kind::RetargetCall, 0x5B9DD5, "E8 B6 FA 62 00", Thunk::AnyRemoteInVehicleBox, ""},
    {"ZUc10", Kind::Bytes, 0xBE9890, "83 EC 18 D9 01 D9 1C 24", Thunk::None, ""},
};

// ZU6: the waiting prompt names every remote asked, through the replay prompt thunk of MF17 (mission_flow_sites.h).
inline constexpr Site kWaitingPrompt[] = {
    {"ZU6", Kind::CallBlock, 0x5BBA5E, "E8 1D E9 30 00 05 CC 00 00 00", Thunk::AskedMemberNames, "90 90 90 90 90"},
};

// ZS1 to ZS5: the zombie spawn picker's view and distance tests cover every remote. ZS1 fills the player array's second entry and the
// extras, ZS2 and ZS4 read entry n of the array and the extras, ZS3 and ZS5 raise the two loop bounds from 2 to 12.
inline constexpr Site kSpawnTests[] = {
    {"ZS1", Kind::CallBlock, 0x5B93E6,
     "74 24 8B 0D 3C 04 17 02 8B C1 3B C3 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 3B C3 75 EE 33 C0",
     Thunk::ZombieSpawnPlayers, "EB 21"},
    {"ZSc1", Kind::Bytes, 0x5B940E, "33 FF 39 5C 24 20 89 44", Thunk::None, ""},
    {"ZS2", Kind::CallBlock, 0x5B9428, "8B 54 24 18 8B 44 94 48", Thunk::ZombieSpawnPlayerAtEax, "90 90 90"},
    {"ZSc2", Kind::Bytes, 0x5B9430, "3B C3 0F 84 27 01 00 00", Thunk::None, ""},
    {"ZS3", Kind::Bytes, 0x5B9566, "83 F8 02 89 44 24 18 0F 8C B5 FE FF FF", Thunk::None, "83 F8 0C"},
    {"ZS4", Kind::CallBlock, 0x5B957B, "8B 4C 94 48 3B CB", Thunk::ZombieSpawnPlayerAtEcx, "90"},
    {"ZSc3", Kind::Bytes, 0x5B9581, "0F 84 77 01 00 00 8B 44", Thunk::None, ""},
    {"ZS5", Kind::Bytes, 0x5B9701, "83 FA 02 0F 8C 71 FE FF FF", Thunk::None, "83 FA 0C"},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"start and end", kStartEnd},
    {"wave bonus", kWaveBonus},
    {"trigger zones", kTriggerZones},
    {"waiting prompt", kWaitingPrompt},
    {"spawn tests", kSpawnTests},
};

}  // namespace zombie_uprising_sites
