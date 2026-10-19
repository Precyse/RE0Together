#pragma once
#include <cstdint>

#include "thunk_frame.h"

// Naked stubs of milestone 16 wave 2 that hand a replaced block of game code to C++ with every register and the flags saved.
// Registers are written back by popad, so a C++ function sets what the block leaves in a register through its frame.

// What a stub entered by a call leaves for its C++ side: the pushad registers, the flags, the return address and the stack
// arguments of the code that called it.
struct BlockFrame {
    guest_call::Regs regs;
    uint32_t flags;
    uint32_t returnAddress;
    uint32_t args[1];
};

// What a stub entered by a jump leaves: the registers, the flags and the game's own stack as it was at the jump.
struct JumpFrame {
    guest_call::Regs regs;
    uint32_t flags;
    uint32_t gameStack[1];
};

// A block replaced by a call: `impl(BlockFrame*)` runs, then the code after the call goes on.
#define DIVERSION_BLOCK_STUB(name, impl) \
    void __declspec(naked) name() { \
        __asm pushfd __asm pushad __asm push esp __asm call impl __asm add esp, 4 __asm popad __asm popfd __asm ret \
    }

// A block replaced by a jump: `impl(JumpFrame*)` runs, then the code goes on at `resume` (a const uintptr_t).
#define DIVERSION_JUMP_STUB(name, impl, resume) \
    void __declspec(naked) name() { \
        __asm pushfd __asm pushad __asm push esp __asm call impl __asm add esp, 4 __asm popad __asm popfd \
        __asm jmp dword ptr [resume] \
    }

// The dword at [esp + offset] of the code that called a block stub, as that code saw it before the call.
inline uint32_t* callerSlot(BlockFrame* frame, uint32_t offset) {
    return reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(&frame->returnAddress) + sizeof(uint32_t) + offset);
}

// The dword at [esp + offset] of the game code that jumped to a stub.
inline uint32_t* gameSlot(JumpFrame* frame, uint32_t offset) {
    return reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(&frame->gameStack[0]) + offset);
}
