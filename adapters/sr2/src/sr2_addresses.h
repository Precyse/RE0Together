#pragma once
#include <cstdint>

// Addresses of SR2_pc.exe (Steam build 1076239 with the Juiced patch, image base 0x400000). Source: docs/SR2_NOTES.md.
namespace sr2 {

// Functions.
constexpr uintptr_t kNetSetMode = 0x7F6F50;           // cdecl(int mode); mode 1 = co-op
constexpr uintptr_t kSessionPoolSetup = 0x7F91C0;     // sizes the connection and packet pools from the limits below
constexpr uintptr_t kRemotePlayerGetter = 0x9D7BD0;   // first human that is not the local player, or null
constexpr uintptr_t kPlayerSlotAllocator = 0x9D7C10;  // refuses a slot index >= kMaxPlayers
constexpr uintptr_t kJoinQueueAdd = 0x810220;         // stdcall(session, request); queues a join request

// Net limits that net_set_mode writes per mode.
constexpr uintptr_t kMaxSessionPlayers = 0xE8C434;
constexpr uintptr_t kMaxPlayers = 0xE8C438;
constexpr uintptr_t kConnectionsPerPlayer = 0xE8C448;

// net_set_mode's co-op case stores ebp (= 2) into the two limits with these instructions.
constexpr uintptr_t kStoreMaxSessionPlayers = 0x7F71EC;
constexpr uintptr_t kStoreMaxPlayers = 0x7F71F2;

constexpr int kNetModeCoop = 1;

// The session object's join request queue count.
constexpr uintptr_t kSessionJoinQueueCountOffset = 0x7EC;

// The player human pool holds 12 slots; co-op ships with 2.
constexpr uint32_t kPoolPlayerSlots = 12;
constexpr uint32_t kCoopDefaultPlayers = 2;

// PE header TimeDateStamp of the supported exe (Juiced's patched output keeps the original's).
constexpr uint32_t kExeTimeDateStamp = 0x49904C37;

}  // namespace sr2
