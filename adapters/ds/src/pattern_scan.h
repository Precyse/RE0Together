#pragma once
// Byte-pattern search in the game's own code, so addresses survive game patches that move functions.
#include <cstdint>

namespace pattern_scan {

// First match of `pattern` ("48 8B 0D ?? ?? ?? ??", "??" = any byte) in the main module's .text, or 0.
uintptr_t find(const char* pattern);

// Address a rip-relative operand points at: the disp32 sits at `match + dispOffset` and the instruction ends at
// `match + instructionEnd`.
uintptr_t ripTarget(uintptr_t match, int dispOffset, int instructionEnd);

}  // namespace pattern_scan
