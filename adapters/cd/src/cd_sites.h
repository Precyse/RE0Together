#pragma once
// Fixed addresses of the supported build (PE TimeDateStamp 0x6AEB7D59, Steam build 26448710, no ASLR) and the first
// 16 bytes expected at every hook site. A set installs only when every one of its rows matches (build_gate::setIntact).
// Re-pinned on 2026-11-05 from the 0x6AB28F00 build: each function re-found by its longest unique byte prefix or by its
// place in the .pdata order (ProcessRecvQueue saves two more registers now), the funnels' argument handling compared
// instruction by instruction, WorldSystem from 9 matching RIP-relative references. The equipment writer was ported by
// tools/cd/port_sites.py (its only call site, inside the equip logic) and compared instruction by instruction: same
// arguments and body, only the OtherEquipItemAck opcode it writes changed (2757 to 3136).
#include <array>
#include <cstdint>

namespace cd_sites {

constexpr size_t kSiteBytes = 16;

struct Site {
    const char* name;
    uintptr_t address;
    std::array<uint8_t, kSiteBytes> bytes;
    const char* set;  // the hook set the row belongs to
};

// Hook sets.
constexpr const char* kFunnels = "funnels";
constexpr const char* kTick = "tick";
constexpr const char* kEquipment = "equipment";
constexpr const char* kMinimap = "minimap";
constexpr const char* kContainment = "containment";
constexpr const char* kStory = "story";

constexpr Site kPostPacket{"ClientNet::PostPacket", 0x1403A6030,
    {0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41}, kFunnels};
constexpr Site kOnRecvPacket{"ClientNet::OnRecvPacket", 0x1403A63C0,
    {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55}, kFunnels};
constexpr Site kProcessRecvQueue{"ClientNet::ProcessRecvQueue", 0x1403A6B30,
    {0x48, 0x8B, 0xC4, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48}, kTick};

// The in-process server's OtherEquipItemAck writer (rcx server actor, r8 u32* serial, r9 {items*, u32 count}); every
// equip path ends here, and with one session it returns before serialising.
constexpr Site kEquipWriter{"ServerOtherEquipWriter", 0x142C740C0,
    {0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55}, kEquipment};

// The client's minimap icon decision (component, u16* icon, u16* sub icon, u8*, u8*); component +0x08 is the actor.
// Ported with its decision tree; both still read the icon table through the two roots below with the same chain.
constexpr Site kMinimapIcon{"ClientMinimapIconDecide", 0x140AEE400,
    {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55}, kMinimap};

// The client character-control component's per-frame update and post-update (rcx = the component, +0x08 actor,
// action layers at +0x88 / +0x90). Ported by signature; heads and layer reads unchanged.
constexpr Site kCcUpdate{"ClientCharacterControl::Update", 0x140944E70,
    {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}, kContainment};
constexpr Site kCcPostUpdate{"ClientCharacterControl::PostUpdate", 0x140946060,
    {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83}, kContainment};

// The client's stage lookup (rcx stage manager, rdx Stage** out, r8 u64 instance); the story guard records the manager
// (static_s14_s16.md 3, disassembly read 2026-11-05).
constexpr Site kStageLookup{"ClientSequencerStageManager::Find", 0x142156CA0,
    {0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57}, kStory};

constexpr std::array<Site, 8> kAll{kPostPacket, kOnRecvPacket, kProcessRecvQueue, kEquipWriter, kMinimapIcon, kCcUpdate,
                                   kCcPostUpdate, kStageLookup};

// Globals (not hooked, read only).
constexpr uintptr_t kWorldSystem = 0x1471BDEF0;  // WorldSystem*, non-null from the title screen on
// UI icon table: u16 icon keys at [[[[root]]+0x08]+0x270]; the game reads the second root under a TLS flag.
constexpr uintptr_t kMinimapIconRoot = 0x1471BE288;
// The client's pa::FieldTime (32 bytes; GetCurrentFieldTime 0x1422A8F70 carries it forward).
constexpr uintptr_t kFieldTimeClient = 0x146EAB998;
// The client clock of 0x1415F9B20: [ms] + [us] / 1000, plus the server bias the ServerTimeInfoAck handler writes; read
// from that function's own instructions on this build.
constexpr uintptr_t kClientClockMs = 0x1470D9E60;
constexpr uintptr_t kClientClockUs = 0x1470D9E70;
constexpr uintptr_t kServerClockBias = 0x1471BE198;
constexpr uintptr_t kMinimapIconRootAlt = 0x1471BE290;

}  // namespace cd_sites
