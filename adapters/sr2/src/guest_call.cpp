#include "guest_call.h"

// The assembly reads the arguments from the stack, which the compiler cannot see.
#pragma warning(disable : 4100)

namespace guest_call {

void __declspec(naked) __cdecl callWithRegs(uintptr_t target, Regs* regs, const uint32_t* args, int count) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 20]
        mov edx, [ebp + 16]
    pushNext:
        test ecx, ecx
        jz pushed
        push dword ptr [edx + ecx * 4 - 4]
        dec ecx
        jmp pushNext
    pushed:
        mov eax, [ebp + 12]
        mov edi, [eax]
        mov esi, [eax + 4]
        mov ebx, [eax + 16]
        mov edx, [eax + 20]
        mov ecx, [eax + 24]
        mov eax, [eax + 28]
        call dword ptr [ebp + 8]
        push eax
        mov eax, [ebp + 12]
        mov [eax], edi
        mov [eax + 4], esi
        mov [eax + 16], ebx
        mov [eax + 20], edx
        mov [eax + 24], ecx
        pop dword ptr [eax + 28]
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

}  // namespace guest_call
