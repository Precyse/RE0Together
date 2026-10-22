#pragma once
#include <cstdint>

#include "weapon_fx_wire.h"

// The engine side of the weapon effects (partner_mode=state): the signatures of the eight primitives the owner hooks,
// and the replay of one recorded entry on the puppet through the same functions. Every call is exception-safe; a fault
// is reported through crash_dump::reportCaught and the call answers false.
namespace weapon_fx_game {

// MSVC cannot declare a free thiscall pointer; __fastcall with edx unused has the same layout.
using PlaySeFunction = int(__fastcall*)(void* weapon, void* edx, uintptr_t chara, uint32_t soundId, float volume);
using GenericSeFunction = int(__stdcall*)(uintptr_t chara, uint32_t soundId, float volume);
using ShootFunction = void(__fastcall*)(void* weapon, void* edx, uintptr_t player, const float* matrix, uint32_t kind);
using MuzzleFlashFunction = void(__fastcall*)(void* player, void* edx, uint32_t index, float scale);
using EffectCreateFunction = uint32_t*(__fastcall*)(void* effects, void* edx, uint32_t* out, uintptr_t owner, uintptr_t set,
                                                    uint32_t index, uintptr_t attach, int32_t joint, const float* pos,
                                                    const float* rot, const uint32_t* flags, uint32_t extra);
using PlayerFunction = void(__fastcall*)(void* player, void* edx);
using RippleFunction = int(__stdcall*)(uintptr_t chara, uint32_t sub, const float* pos);

// The weapon unit a character holds, or 0.
uintptr_t weaponUnitOf(uintptr_t player);

// Runs `entry` on `player`: the primitive its site names, with the puppet in place of the owner's character. False when
// the engine objects it needs are missing, or the call faulted.
bool replay(uintptr_t player, const weapon_fx_wire::Entry& entry);

// The weapon type the character holds (0 when unreadable).
int32_t weaponTypeOf(uintptr_t player);

}  // namespace weapon_fx_game
