#include "damage_thunk.h"

#include <windows.h>

#include "thunk_emit.h"

namespace {

// Entry stack: [ret][attacker][distance][info]. Five pushes rebuild the arguments right to left; each push moves
// the remaining source slots by 4, so the same [esp+16] displacement reads info, distance, then attacker.
constexpr uint8_t kSourceDisplacement = 16;
constexpr uint16_t kCalleeArgumentBytes = 12;

void emitThunk(uint8_t* p, damage_thunk::Handler handler, uintptr_t original) {
    thunk::emit(p, {0x68});  // push original
    thunk::emitAddress(p, reinterpret_cast<const void*>(original));
    for (int i = 0; i < 3; ++i) thunk::emit(p, {0xFF, 0x74, 0x24, kSourceDisplacement});  // push info, distance, attacker
    thunk::emit(p, {0x51});  // push ecx (enemy)
    thunk::emit(p, {0xB8});  // mov eax, handler
    thunk::emitAddress(p, reinterpret_cast<const void*>(handler));
    thunk::emit(p, {0xFF, 0xD0});  // call eax (stdcall: pops its five arguments)
    thunk::emit(p, {0xC2, static_cast<uint8_t>(kCalleeArgumentBytes), 0x00});  // ret 0xC
}

}  // namespace

namespace damage_thunk {

uintptr_t patchVtable(uintptr_t vtable, uint8_t* memory, Handler handler) {
    const uintptr_t slotAddress = vtable + game::kEnemyDamageSlot * sizeof(uint32_t);
    const uintptr_t original = game::readPointer(slotAddress);
    if (!original) return 0;
    emitThunk(memory, handler, original);
    FlushInstructionCache(GetCurrentProcess(), memory, kThunkSize);
    return game::writeProtected(slotAddress, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory))) ? original : 0;
}

}  // namespace damage_thunk
