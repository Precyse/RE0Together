#pragma once
#include <cstdint>

#include "guest_call.h"

// Naked stubs that hand a game call to a C++ function. The stubs do nothing but build the arguments, so every
// register the C++ function does not return stays as the game left it.

// What PARTNER_WRAP_STUB leaves on the stack for its C++ side: the pushad registers, then the return address and the
// stack arguments of the wrapped function. The C++ side writes its result into `regs` (popad loads it).
struct GuestFrame {
    guest_call::Regs regs;
    uint32_t returnAddress;
    uint32_t args[1];
};

// Detour of a game function: `impl(GuestFrame*)` runs with all registers saved, then the function returns popping
// `popBytes` of stack arguments.
#define PARTNER_WRAP_STUB(name, impl, popBytes) \
    void __declspec(naked) name() { __asm pushad __asm push esp __asm call impl __asm add esp, 4 __asm popad __asm ret popBytes }

// Replacement of a call whose input is register `reg`: `impl(reg)` returns in eax; eax, ecx and edx are clobbered.
#define PARTNER_REG_STUB(name, reg, impl) \
    void __declspec(naked) name() { __asm push reg __asm call impl __asm add esp, 4 __asm ret }

// Replacement of a block of stores or a call, entered by a call: `impl()` runs with all registers and the flags saved (the
// x87 stack is not touched), then the code after the block goes on. `impl` makes the stores the block made.
#define PARTNER_BLOCK_STUB(name, impl) \
    void __declspec(naked) name() { __asm pushfd __asm pushad __asm call impl __asm popad __asm popfd __asm ret }

// Replacement of a block in the middle of a function, jumped out of: `impl()` runs with all registers saved, then the function goes on
// at the address held in `resume`. The block's stores are `impl`'s to make; the caller knows that no register or flag it left is live.
#define PARTNER_RESUME_STUB(name, impl, resume) \
    void __declspec(naked) name() { __asm pushad __asm call impl __asm popad __asm jmp dword ptr [resume] }

// Replacement of a block that ends by going on at one of several addresses (a resume point), entered by a jump: `impl()`
// returns the address, with all registers saved. The slot under the saved registers receives it and `ret` jumps there, every
// register as the game left it and the stack as it was before the jump. The flags are not kept.
#define PARTNER_RESUME_AT_STUB(name, impl) \
    void __declspec(naked) name() { __asm sub esp, 4 __asm pushad __asm call impl __asm mov [esp + 0x20], eax __asm popad __asm ret }

// The same for `impl(ebx)`.
#define PARTNER_RESUME_AT_EBX_STUB(name, impl) \
    void __declspec(naked) name() { \
        __asm sub esp, 4 __asm pushad __asm push ebx __asm call impl __asm add esp, 4 __asm mov [esp + 0x20], eax __asm popad __asm ret \
    }

// Entry trace of a game function: `impl(GuestFrame*)` runs with all registers and the flags saved, then the function
// runs as the game has it. `original` holds the MinHook trampoline.
#define TRACE_ENTRY_STUB(name, impl, original) \
    void __declspec(naked) name() { \
        __asm pushad __asm pushfd __asm lea eax, [esp + 4] __asm push eax __asm call impl __asm add esp, 4 \
        __asm popfd __asm popad __asm jmp dword ptr [original] \
    }

// Entry and exit of a function whose result is logged. `enter(GuestFrame*)` may swap the frame's return address for
// `exitName`, which the caller declares before `enter`; the exit stub then calls `leave(eax)`, which returns the real
// return address. Registers and flags survive both.
#define TRACE_RETURN_STUBS(entryName, exitName, enter, leave, original) \
    TRACE_ENTRY_STUB(entryName, enter, original) \
    void __declspec(naked) exitName() { \
        __asm push 0 __asm pushad __asm pushfd __asm push eax __asm call leave __asm add esp, 4 \
        __asm mov [esp + 36], eax __asm popfd __asm popad __asm ret \
    }
