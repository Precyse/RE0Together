#include "update_thunk.h"

#include <windows.h>

#include "thunk_emit.h"

namespace {

void emitThunk(uint8_t* p, update_thunk::Handler handler, uintptr_t original) {
    thunk::emit(p, {0x68});  // push original
    thunk::emitAddress(p, reinterpret_cast<const void*>(original));
    thunk::emit(p, {0x51});  // push ecx (enemy)
    thunk::emit(p, {0xB8});  // mov eax, handler
    thunk::emitAddress(p, reinterpret_cast<const void*>(handler));
    thunk::emit(p, {0xFF, 0xD0});  // call eax (stdcall: pops its two arguments)
    thunk::emit(p, {0xC3});        // ret
}

}  // namespace

namespace update_thunk {

uintptr_t patchVtable(uintptr_t vtable, uint8_t* memory, Handler handler) {
    const uintptr_t slotAddress = vtable + game::kEnemyUpdateSlot * sizeof(uint32_t);
    const uintptr_t original = game::readPointer(slotAddress);
    if (!original) return 0;
    emitThunk(memory, handler, original);
    FlushInstructionCache(GetCurrentProcess(), memory, kThunkSize);
    return game::writeProtected(slotAddress, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory))) ? original : 0;
}

}  // namespace update_thunk
