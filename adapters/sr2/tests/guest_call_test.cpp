// callWithRegs against stand-in game functions: registers in and out, stack arguments, and either side popping them.
#include <cstdio>

#include "../src/guest_call.h"
#include "check.h"

namespace {

// eax = ecx + edx + esi + [esp+4] - [esp+8]; leaves edi = 7 and ebx = 9. The caller pops the arguments.
void __declspec(naked) callerPops() {
    __asm {
        mov eax, ecx
        add eax, edx
        add eax, esi
        add eax, [esp + 4]
        sub eax, [esp + 8]
        mov edi, 7
        mov ebx, 9
        ret
    }
}

// eax = ebx + [esp+4]; the function pops its one argument.
void __declspec(naked) calleePops() {
    __asm {
        mov eax, ebx
        add eax, [esp + 4]
        ret 4
    }
}

// Reads nothing, returns the value edi had.
void __declspec(naked) edgeOnly() {
    __asm {
        mov eax, edi
        ret
    }
}

}  // namespace

int main() {
    guest_call::Regs regs{.esi = 1000, .edx = 20, .ecx = 3};
    const uint32_t args[] = {100, 10};
    guest_call::callWithRegs(reinterpret_cast<uintptr_t>(&callerPops), &regs, args, 2);
    check(regs.eax == 3 + 20 + 1000 + 100 - 10, "registers in, arguments in order, eax out");
    check(regs.edi == 7 && regs.ebx == 9, "registers the function changed come back");
    check(regs.esi == 1000 && regs.edx == 20 && regs.ecx == 3, "registers it kept stay");

    uint32_t before = 0;
    uint32_t after = 0;
    __asm {
        mov before, esp
    }
    guest_call::Regs second{.ebx = 5};
    const uint32_t one[] = {40};
    guest_call::callWithRegs(reinterpret_cast<uintptr_t>(&calleePops), &second, one, 1);
    __asm {
        mov after, esp
    }
    check(second.eax == 45 && before == after, "a function that pops its own argument leaves the stack as it was");

    check(guest_call::call(reinterpret_cast<uintptr_t>(&edgeOnly), {.edi = 77}) == 77, "call() returns eax");
    check(guest_call::call(reinterpret_cast<uintptr_t>(&callerPops), {.esi = 1}, {5, 2}) == 4, "call() with arguments");
    return g_failures == 0 ? 0 : 1;
}
