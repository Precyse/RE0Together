#pragma once
#include "partner_sites.h"

// Where milestone 16 W2 changes the exe for mission start, the replay prompt and the mission bonus with 3 or 4 players
// (docs/SR2_NOTES.md, "Mission flow (milestone 16 W2; MF1 to MF23)"). Copied from the research table of M16_W2_MISSIONFLOW.md; the
// bytes are what the exe has there. MF22 of the research (0x53FF08, the look sync after a reward item) is OF6 of outfit_sync_sites.h
// and is not repeated. Each table is one phase of multiplayer_sites.h, written all or nothing.
namespace mission_flow_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// MF1 to MF4: the host answers a guest's mission ask with the guest that asked as the owner. MF5: a start reply goes to the one member
// that should get it. MF6: the first accept starts the mission, later ones are dropped and the other prompts are closed. MF7 to
// MF10: a retry or replay keeps the remote already in the pair. MF11: the wait dialog names every remote asked.
inline constexpr Site kPrompts[] = {
    {"MF1", Kind::HookEntry, Gate::Now, 0x6A8450, "81 EC 24 05 00 00 A1 50", Thunk::StartPromptAnswer, ""},
    {"MF2", Kind::RetargetCall, Gate::Now, 0x6A84D6, "E8 F5 F6 32 00", Thunk::StartRequesterHuman, ""},
    {"MF3", Kind::RetargetCall, Gate::Now, 0x6A84E3, "E8 E8 F6 32 00", Thunk::StartRequesterHuman, ""},
    {"MF4", Kind::RetargetCall, Gate::Now, 0x68FE48, "E8 33 88 01 00", Thunk::RememberStartRequester, ""},
    {"MF5", Kind::RetargetCall, Gate::Now, 0x6A8425, "E8 16 C3 16 00", Thunk::SendStartReply, ""},
    {"MF6", Kind::JumpOut, Gate::Now, 0x6A8B54, "39 05 8C 3C 7B 02 0F 84 38 01 00 00", Thunk::FirstAcceptStarts, ""},
    {"MF7", Kind::RetargetCall, Gate::Now, 0x6A70DA, "E8 F1 0A 33 00", Thunk::PairRemoteHuman, ""},
    {"MF8", Kind::RetargetCall, Gate::Now, 0x6A70E3, "E8 E8 0A 33 00", Thunk::PairRemoteHuman, ""},
    {"MF9", Kind::RetargetCall, Gate::Now, 0x6A7109, "E8 C2 0A 33 00", Thunk::PairRemoteHuman, ""},
    {"MF10", Kind::RetargetCall, Gate::Now, 0x6A7112, "E8 B9 0A 33 00", Thunk::PairRemoteHuman, ""},
    {"MF11", Kind::CallBlock, Gate::Now, 0x69DF8B, "E8 40 9C 33 00 05 E0 12 00 00", Thunk::AskedHumanNames, "90 90 90 90 90"},
};

// MF12 to MF17: the replay prompt. Guests talk to the host only, an answer goes to the member that asked, a decline of the host's ask
// counts once every remote has declined, and the wait dialog names every remote asked.
inline constexpr Site kReplay[] = {
    {"MF12", Kind::RetargetCall, Gate::Now, 0x5441EC, "E8 4F 05 2D 00", Thunk::SendGuestToHost, ""},
    {"MF13", Kind::HookEntry, Gate::Now, 0x543D70, "8B 44 24 04 8B 00 8D 4C", Thunk::RememberReplayRequester, ""},
    {"MF14", Kind::RetargetCall, Gate::Now, 0x543CD0, "E8 6B 0A 2D 00", Thunk::SendReplayAnswer, ""},
    {"MF15", Kind::HookEntry, Gate::Now, 0x543F10, "81 EC 14 02 00 00 A1 50", Thunk::ReplayAnswerTally, ""},
    {"MF16", Kind::RetargetCall, Gate::Now, 0x544DDE, "E8 5D F9 2C 00", Thunk::SendGuestToHost, ""},
    {"MF17", Kind::CallBlock, Gate::Now, 0x544279, "E8 02 61 38 00 05 CC 00 00 00", Thunk::AskedMemberNames, "90 90 90 90 90"},
};

// MF18 to MF21: the act 7 mission bonus carries a flag per recipient, the objective bonus reaches every remote, the co-op stat event
// counts any remote.
inline constexpr Site kRewards[] = {
    {"MF18", Kind::HookEntry, Gate::Now, 0x6A9640, "81 EC D0 05 00 00 A1 50", Thunk::RememberBonusHuman, ""},
    {"MF19", Kind::RetargetCall, Gate::Now, 0x6A9796, "E8 A5 AF 16 00", Thunk::SendBonusPerRecipient, ""},
    {"MF20", Kind::RetargetCall, Gate::Now, 0x6A9F79, "E8 C2 F6 FF FF", Thunk::ObjectiveBonusEachRemote, ""},
    {"MF21", Kind::RetargetCall, Gate::Now, 0x95D6D1, "E8 FA A4 07 00", Thunk::MatchRemoteEsi, ""},
};

// trace=1 only: the mission state change with the pair it finds.
inline constexpr Site kTrace[] = {
    {"MF23", Kind::HookEntry, Gate::Now, 0x6A1250, "A1 A8 7C 52 02 85 C0 74", Thunk::TraceMissionState, ""},
};

}  // namespace mission_flow_sites
