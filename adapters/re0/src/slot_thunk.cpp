#include "slot_thunk.h"

#include <windows.h>

#include "thunk_emit.h"

namespace {

constexpr size_t kArgumentBytes = sizeof(uint32_t);

// Entry stack: [ret][arg0]... The thunk hands the handler (enemy, original, &arg0) and returns popping its arguments.
void emitThunk(uint8_t* p, slot_thunk::Handler handler, uintptr_t original, size_t argc) {
    thunk::emit(p, {0x8D, 0x44, 0x24, 0x04});  // lea eax, [esp+4]
    thunk::emit(p, {0x50});                    // push eax (&arg0)
    thunk::emit(p, {0x68});                    // push original
    thunk::emitAddress(p, reinterpret_cast<const void*>(original));
    thunk::emit(p, {0x51});  // push ecx (enemy)
    thunk::emit(p, {0xB8});  // mov eax, handler
    thunk::emitAddress(p, reinterpret_cast<const void*>(handler));
    thunk::emit(p, {0xFF, 0xD0});  // call eax (stdcall: pops its three arguments)
    const uint16_t popBytes = static_cast<uint16_t>(argc * kArgumentBytes);
    thunk::emit(p, {0xC2, static_cast<uint8_t>(popBytes & 0xff), static_cast<uint8_t>(popBytes >> 8)});  // ret popBytes
}

}  // namespace

namespace slot_thunk {

uintptr_t patchVtable(uintptr_t vtable, size_t slot, uint8_t* memory, size_t argc, Handler handler) {
    const uintptr_t slotAddress = vtable + slot * sizeof(uint32_t);
    const uintptr_t original = game::readPointer(slotAddress);
    if (!original) return 0;
    emitThunk(memory, handler, original, argc);
    FlushInstructionCache(GetCurrentProcess(), memory, kThunkSize);
    return game::writeProtected(slotAddress, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory))) ? original : 0;
}

}  // namespace slot_thunk
