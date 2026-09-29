#pragma once
// RE0 HD (Steam build 17178773, re0hd.exe, no ASLR). Addresses and offsets from docs/RE0_NOTES.md.
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace game {

// Singleton pointer globals
constexpr uintptr_t kPlayerGlobal = 0xdcbf3c;      // sPlayer*
constexpr uintptr_t kGameCharaGlobal = 0xdcc0d0;   // sGameChara*
constexpr uintptr_t kGamePadGlobal = 0xe2d7c8;     // sGamePad*
constexpr uintptr_t kGameInfoGlobal = 0xdcbe9c;    // sGameInfo*
constexpr uintptr_t kDoorLoadGlobal = 0xdcbeb8;    // sDoorLoad*
constexpr uintptr_t kSubMenuGlobal = 0xdcebd0;     // sSubMenu*
constexpr uintptr_t kItemGlobal = 0xdcbf44;        // sItem*
constexpr uintptr_t kFlagManagerGlobal = 0xdcc014; // sFlagManager* (size 0x144, vtable 0xcd9184)

// sFlagManager: story flags as a bitset of flag_diff::kWords dwords (0x11c bytes; set(index, count, value) is 0x59c980,
// the save copy 0x59ba30 copies the same 0x47 dwords)
constexpr uintptr_t kFlagBitsOffset = 0x20;

// sPlayer
constexpr uintptr_t kPlayerControlledOffset = 0x2c;
constexpr uintptr_t kPlayerPartnerOffset = 0x3c;
constexpr uintptr_t kPlayerFollowOffset = 0x40;  // u8: 1 while the partner follows (the game's own E toggles it)

// sGameChara mirrors the sPlayer pair
constexpr uintptr_t kGameCharaControlledOffset = 0x148c;
constexpr uintptr_t kGameCharaPartnerOffset = 0x149c;

// uPlayerBase (uPlayerBilly / uPlayerRebecca)
constexpr size_t kPlayerBaseSize = 0x6AD0;
constexpr uintptr_t kPlayerPositionOffset = 0x40;     // vec3 + pad
constexpr uintptr_t kPlayerRotationOffset = 0x50;     // quaternion xyzw
constexpr uintptr_t kPlayerScaleOffset = 0x60;        // vec3
constexpr uintptr_t kPlayerWorldMatrixOffset = 0x70;  // 4x4, current
constexpr uintptr_t kPlayerPrevWorldMatrixOffset = 0xB0;
constexpr uintptr_t kPlayerThinkOffset = 0x67b8;
constexpr uintptr_t kThinkSetterFunction = 0x508b1e;

// Character classes, told apart by the object's vptr (the tracer patches vtable entries, not the vptr)
constexpr uintptr_t kBillyVtable = 0xccf5a0;     // uPlayerBilly
constexpr uintptr_t kRebeccaVtable = 0xccfd10;   // uPlayerRebecca

constexpr uintptr_t kPlayerHpOffset = 0x1030;  // i32
constexpr uintptr_t kPlayerFlagsOffset = 0xc;  // u32 attribute flags
constexpr uint32_t kPlayerInCurrentRoomFlag = 0x4000;

// sItem: one fixed inventory block per character (6 slots of {u32 itemId, u32 count} plus 0x10 of extras)
constexpr uintptr_t kItemRebeccaBlockOffset = 0x24;
constexpr uintptr_t kItemBillyBlockOffset = 0x64;
constexpr size_t kInventoryBlockSize = 0x40;

// sItemPut: the dropped floor items, 28 records; a live record points at its spawned uItem
constexpr uintptr_t kItemPutGlobal = 0xdce0a8;  // sItemPut*
constexpr uintptr_t kItemPutRecordsOffset = 0x20;
constexpr size_t kItemPutRecordCount = 28;
constexpr int32_t kItemPutEmptyState = -1;
constexpr uintptr_t kUnitPositionOffset = 0x40;  // vec3, every MT unit (uItem included)
constexpr uintptr_t kItemPutFunction = 0x4de500;     // thiscall on sItemPut, (ItemDesc*, position*, euler rotation*), ret 0xC, returns the uItem
constexpr uintptr_t kItemRemoveFunction = 0x4de730;  // thiscall, (uItem*), ret 4; loads its own `this` from [0xdcbf40], ecx is ignored

// Pickup action step (reached through the handler table at 0xd8ac6c): thiscall on the action state, (player), ret 4.
// Phase 1 waits for the animation, looks the item up by key without a null check and takes it.
constexpr uintptr_t kPickupStepFunction = 0x500d00;
constexpr uintptr_t kPickupPhaseOffset = 0x10;  // u32 phase of the action state: 0 start, 1 take, 2+ done
constexpr uintptr_t kPickupPlayerOffset = 0x18;  // the player doing the pickup
// Player field: the interaction target (the item being picked up); the game clears it when that item is removed.
constexpr uintptr_t kPlayerInteractTargetOffset = 0x67a0;
constexpr uint32_t kPickupTakePhase = 1;
constexpr uint32_t kPickupDonePhase = 2;

struct ItemDesc {
    uint32_t itemId;
    uint32_t count;
    uint32_t reserved;
};
static_assert(sizeof(ItemDesc) == 12);

struct ItemPutRecord {
    uint32_t key;
    uint32_t reserved0;
    uint32_t itemId;
    uint32_t count;
    uint32_t item;  // uItem*
    int32_t state;  // kItemPutEmptyState when the record is free
    uint32_t reserved1[3];
};
static_assert(sizeof(ItemPutRecord) == 0x24);

// Rooms, doors and menus (single-byte reads are enough for every value below except the door phase, an i32)
constexpr uintptr_t kGameInfoStageOffset = 0x2a80;
constexpr uintptr_t kGameInfoRoomOffset = 0x2a84;
constexpr uintptr_t kDoorLoadStateOffset = 0x44;
constexpr uintptr_t kRoomControlGlobal = 0xdcbeb4;         // sRoomControl*
constexpr uintptr_t kRoomPhaseCurrentOffset = 0xb8 + 0x14;  // phase manager +0x14: room_phase::Phase
// sRoomControl::requestPhase: thiscall (phase), ret 4. The vanilla V requests Change (9), which zaps to the partner
// and loads its room when the two are apart (player think 0x4fed48 / 0x50395e).
constexpr uintptr_t kRequestRoomPhaseFunction = 0x610e00;
// sDoorLoad::start: thiscall (room, entry, a, b, flag), ret 0x14. Every room change starts here: it plays the door,
// counts down and then calls sRoomControl::changeRoom (0x610c60), which carries the partner when sPlayer +0x40 is set
// and partner +0xff4 equals the scene's current room record.
constexpr uintptr_t kDoorStartFunction = 0x552b50;
// Event-script condition "the controlled player acts on this trigger": cdecl bool(void*, context*), true when the
// controlled player's trigger zone (player +0x16e4, also kept for the partner) equals context +8, pad 0 action is
// pressed and the player may act. Doors and other in-room interactions go through it.
constexpr uintptr_t kActOnTriggerFunction = 0x564070;
constexpr uintptr_t kSubMenuStateOffset = 0x2c;
constexpr uintptr_t kSubMenuOpenFunction = 0x5d9030;  // thiscall, no args: opens the inventory for the focused character
constexpr uint8_t kSubMenuClosed = 0x0d;
// cPlayerThink vtable slot 28, reached from the lethal hit: kills the player (setHP 0) and enters the dead state.
// Save data owner check: compares the SteamID stamped in the save (+0xa0) with SteamUser()->GetSteamID().
constexpr uintptr_t kSaveOwnerCheckFunction = 0x612600;  // thiscall, no args, returns bool
constexpr uintptr_t kPlayerOnDeathFunction = 0x4fcea0;  // thiscall on the think, 1 stack arg (player), ret 4

// Enemies: sEnemy holds a 37-entry pool; the pool slot index is the enemy's network id.
constexpr uintptr_t kEnemyGlobal = 0xdcdc78;  // sEnemy*
constexpr uintptr_t kEnemyPoolOffset = 0x4b0;
constexpr uintptr_t kEnemyPoolEntrySize = 16;
constexpr uintptr_t kEnemyPoolObjectOffset = 0xc;  // uEnemy* inside a pool entry
constexpr int kEnemyPoolSlots = 37;
constexpr uintptr_t kEnemyHpOffset = 0x1030;  // i32, dead enemies hold -1
constexpr uintptr_t kSetHpFunction = 0x529310;  // thiscall, 1 stack arg; enemies and players
constexpr size_t kEnemyDamageSlot = 35;  // vtable slot (+0x8c): damage(attacker, float distance, HitInfo*), thiscall ret 0xC
constexpr std::array<uintptr_t, 38> kEnemyVtables = {
    0xcbdcd8, 0xcc4f28, 0xcc50a0, 0xcc5218, 0xcc5390, 0xcc5508, 0xcc5680, 0xcc57f8, 0xcc5970, 0xcc3fe8,
    0xcbf0d0, 0xcbf5f8, 0xcbf9a0, 0xcbfbc8, 0xcbfe58, 0xcc01a8, 0xcc0458, 0xcc08c0, 0xcc5ae8, 0xcc0cb0,
    0xcc13d8, 0xcc1618, 0xcc18e8, 0xcc5c60, 0xcc1ce8, 0xcc1e60, 0xcc1fd8, 0xcc25d8, 0xcc2b38, 0xcc2f70,
    0xcc3248, 0xcc3710, 0xcc5dd8, 0xcc5f50, 0xcc60c8, 0xcc6240, 0xcc63b8, 0xcc6530};

// Argument of an enemy's damage function, built by the hit resolver (0x5313b0).
struct HitInfo {
    int32_t rangeTier;
    int32_t attackType;
    int32_t a;
    int32_t b;
    void* attacker;
    uint8_t flag;
    uint8_t reserved[3];
};
static_assert(sizeof(HitInfo) == 24);

// Think (brain) objects
constexpr uintptr_t kControlledThinkVtable = 0xccbbe8;  // cPlayerThink
constexpr uintptr_t kPartnerThinkVtable = 0xcc99b8;     // cPlayerSubThink
constexpr uintptr_t kThinkPadSlotOffset = 0x50;         // vtable slot 20, which pad to read
constexpr uintptr_t kControlledThinkPadFunction = 0x4f9f10;  // getPad(0)
constexpr uintptr_t kPartnerThinkPadFunction = 0x4f4be0;     // getPad(1)

// sGamePad
constexpr uintptr_t kGetPadFunction = 0x600630;  // thiscall, 1 stack arg, i < 2
constexpr uintptr_t kPadTableOffset = 0xc24;
constexpr uintptr_t kPadEntrySize = 8;
constexpr uintptr_t kAnalogGetterFunction = 0x600550;  // thiscall, no args, returns a pointer to the analog block
constexpr uintptr_t kPadVtable = 0xce55e0;
constexpr uintptr_t kBlockedPadOffset = 0xc34;  // sGamePad member: the pad getPad returns while input is blocked
constexpr size_t kPadObjectSize = 0x4c0;

// Engine functions (32-bit; thiscall members are invoked through callThiscall)
constexpr uintptr_t kPlayerMove = 0x513190;               // uPlayerBase::move, thiscall, once per frame per player
constexpr uintptr_t kAllocThinkFunction = 0x4f81e0;       // cdecl void*(size, align)
constexpr uintptr_t kPlayerThinkCtorFunction = 0x4f8010;  // thiscall void*(self), returns self
constexpr uintptr_t kSetThinkFunction = 0x50f980;         // thiscall void(player, think), destroys the previous think
constexpr uintptr_t kSetControlledFunction = 0x4ecda0;    // sPlayer::setControlled, thiscall, 1 stack arg
constexpr uintptr_t kSetPartnerFunction = 0x4ece00;       // sPlayer::setPartner, thiscall, 1 stack arg
// sUnit::updateAll (vtable slot 6, thiscall, no args, plain ret), once per frame: walks every unit group through
// slot 9 (0x7279e0, thiscall, group index, ret 4), which calls each unit's vtable slot 8 (the move chain).
constexpr uintptr_t kUnitUpdateAllFunction = 0x727b50;
constexpr uint32_t kPlayerThinkSize = 0xd0;
constexpr uint32_t kPartnerThinkSize = 0x1a0;  // cPlayerSubThink
// Partner code may still treat the partner's think as a cPlayerSubThink and touch its larger layout, so a
// cPlayerThink handed to a partner is allocated at the larger size with the tail zeroed.
constexpr uint32_t kThinkAllocationSize = kPartnerThinkSize > kPlayerThinkSize ? kPartnerThinkSize : kPlayerThinkSize;
constexpr uint32_t kPlayerThinkAlign = 0x10;

// MSVC cannot declare a free thiscall pointer; __fastcall with edx unused has the same register and stack layout.
template <class R, class... Args>
R callThiscall(uintptr_t function, void* self, Args... args) {
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(function)(self, nullptr, args...);
}

inline void* allocThink() {
    void* memory = reinterpret_cast<void*(__cdecl*)(uint32_t, uint32_t)>(kAllocThinkFunction)(kThinkAllocationSize,
                                                                                               kPlayerThinkAlign);
    if (memory) std::memset(memory, 0, kThinkAllocationSize);
    return memory;
}
inline void* constructPlayerThink(void* memory) { return callThiscall<void*>(kPlayerThinkCtorFunction, memory); }
inline void setThink(void* player, void* think) { callThiscall<void>(kSetThinkFunction, player, think); }

// Code decryption probe: the SteamStub-decrypted prologue of cPlayerThink's pad getter
constexpr uintptr_t kDecryptProbeAddress = kControlledThinkPadFunction;
constexpr std::array<uint8_t, 9> kDecryptProbeBytes = {0x8B, 0x0D, 0xC8, 0xD7, 0xE2, 0x00, 0x6A, 0x00, 0xE8};

using PadBytes = std::array<uint8_t, kPadObjectSize>;

// Exception-safe read of a trivially copyable value from game memory.
template <class T>
bool readMemory(uintptr_t address, T& out) {
    if (address == 0) return false;
    __try {
        std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Exception-safe write of a trivially copyable value into game memory.
template <class T>
bool writeMemory(uintptr_t address, const T& value) {
    if (address == 0) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Writes a pointer-sized value into read-only game memory (vtables).
inline bool writeProtected(uintptr_t address, uint32_t value) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), PAGE_READWRITE, &oldProtect)) return false;
    *reinterpret_cast<volatile uint32_t*>(address) = value;
    VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), oldProtect, &oldProtect);
    return true;
}

// Pointer stored at address, or 0 when unreadable.
inline uintptr_t readPointer(uintptr_t address) {
    uint32_t value = 0;
    return readMemory(address, value) ? value : 0;
}

// getPad returns a dummy pad (sGamePad + 0xc34, a different class) while input is blocked; only the real
// pad class answers the query slots this adapter records and replays.
inline bool isRealPad(const void* object) {
    return object && readPointer(reinterpret_cast<uintptr_t>(object)) == kPadVtable;
}

inline bool codeDecrypted() {
    std::array<uint8_t, kDecryptProbeBytes.size()> actual{};
    return readMemory(kDecryptProbeAddress, actual) && actual == kDecryptProbeBytes;
}

// Pointer stored at sPlayer + offset, or 0 when sPlayer or the field is missing.
inline uintptr_t playerField(uintptr_t offset) {
    const uintptr_t player = readPointer(kPlayerGlobal);
    return player ? readPointer(player + offset) : 0;
}

inline uintptr_t controlled() { return playerField(kPlayerControlledOffset); }
inline uintptr_t partner() { return playerField(kPlayerPartnerOffset); }

// Makes `player` the partner with a fresh cPlayerSubThink (partner AI).
inline void setPartner(uintptr_t player) {
    void* sPlayer = reinterpret_cast<void*>(readPointer(kPlayerGlobal));
    if (sPlayer) callThiscall<void>(kSetPartnerFunction, sPlayer, reinterpret_cast<void*>(player));
}

// Queues a room phase (room_phase.h) on sRoomControl.
inline void requestRoomPhase(int32_t phase) {
    void* control = reinterpret_cast<void*>(readPointer(kRoomControlGlobal));
    if (control) callThiscall<void>(kRequestRoomPhaseFunction, control, phase);
}

// Swaps the camera character: `next` becomes the controlled player and `previous` the partner.
inline void swapControlled(uintptr_t next, uintptr_t previous) {
    void* sPlayer = reinterpret_cast<void*>(readPointer(kPlayerGlobal));
    if (!sPlayer) return;
    callThiscall<void>(kSetControlledFunction, sPlayer, reinterpret_cast<void*>(next));
    setPartner(previous);
}

inline bool readTransform(uintptr_t player, float (&pos)[3], float (&quat)[4]) {
    return player && readMemory(player + kPlayerPositionOffset, pos) &&
           readMemory(player + kPlayerRotationOffset, quat);
}

inline bool writeTransform(uintptr_t object, const float (&pos)[3], const float (&quat)[4]) {
    return writeMemory(object + kPlayerPositionOffset, pos) && writeMemory(object + kPlayerRotationOffset, quat);
}

}  // namespace game
