#include "squared_distance.h"

namespace squared_distance {

// Loads the two vec3 pointers and jumps to the game's function: it returns to the caller with the float in ST0, which is where a
// cdecl float result is, and the caller pops the three arguments.
float __declspec(naked) __cdecl between(uintptr_t, uint32_t, uint32_t) {
    __asm {
        mov edx, [esp + 4]
        mov eax, [esp + 8]
        mov ecx, [esp + 12]
        jmp edx
    }
}

}  // namespace squared_distance
