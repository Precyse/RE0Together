#pragma once
#include <cstdint>
#include <initializer_list>

// Calls into game functions that take their inputs in registers.
namespace guest_call {

// The general registers in the order pushad stores them.
struct Regs {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

// Calls the game function at `target` with edi, esi, ebx, edx, ecx and eax loaded from `regs` and `args` pushed right
// to left. Afterwards `regs` holds what the function left in those registers. The stack is restored whether the callee
// or the caller pops the arguments; ebp and esp in `regs` are neither loaded nor stored.
void __cdecl callWithRegs(uintptr_t target, Regs* regs, const uint32_t* args, int count);

// callWithRegs for the usual case: `regs` in, the function's eax out.
inline uint32_t call(uintptr_t target, Regs regs, std::initializer_list<uint32_t> args = {}) {
    callWithRegs(target, &regs, args.begin(), static_cast<int>(args.size()));
    return regs.eax;
}

}  // namespace guest_call
