#pragma once
#include <array>
#include <cstdint>
#include <vector>

// One edit of the exe's code: the bytes that must be at `address` and what to write over their start.
struct CodePatch {
    const char* name;
    uintptr_t address;
    std::vector<uint8_t> expected;
    std::vector<uint8_t> replacement;  // at most expected.size() bytes; empty = only check
};

inline constexpr uint8_t kCallRel32 = 0xE8;
inline constexpr uint8_t kJmpRel32 = 0xE9;

// `call rel32` or `jmp rel32` (`opcode`) placed at `from`, reaching `to`.
inline std::array<uint8_t, 5> relativeBranch(uint8_t opcode, uintptr_t from, uintptr_t to) {
    const uint32_t displacement = static_cast<uint32_t>(to - (from + 5));
    return {opcode, static_cast<uint8_t>(displacement), static_cast<uint8_t>(displacement >> 8),
            static_cast<uint8_t>(displacement >> 16), static_cast<uint8_t>(displacement >> 24)};
}

// Compares every patch's `expected` with the running exe and logs each difference. True when all match.
bool codeMatches(const std::vector<CodePatch>& patches);

// Writes every replacement when all patches match, otherwise writes nothing. True when the code was changed.
bool applyCodePatches(const std::vector<CodePatch>& patches);
