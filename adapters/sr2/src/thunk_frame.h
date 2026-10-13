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
