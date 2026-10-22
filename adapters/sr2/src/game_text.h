#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "memory.h"

// Reads and writes the game's fixed size, zero terminated text buffers.
namespace game_text {

constexpr size_t kPageBytes = 0x1000;

// The text at `address`, at most `capacity - 1` characters; empty when it is not readable. Reads up to each page end,
// so a string that ends at the end of the mapped memory is still read.
inline std::string read(uintptr_t address, size_t capacity) {
    std::string text;
    while (text.size() + 1 < capacity) {
        const uintptr_t next = address + text.size();
        const size_t size = std::min(kPageBytes - next % kPageBytes, capacity - 1 - text.size());
        std::string chunk(size, '\0');
        if (!readMemory(next, chunk.data(), size)) break;
        const size_t length = chunk.find('\0');
        if (length != std::string::npos) return text + chunk.substr(0, length);
        text += chunk;
    }
    return text;
}

// Writes `text` into the buffer of `capacity` bytes at `address` (cut to fit), zero padded. The buffer must be mapped.
inline void write(uintptr_t address, size_t capacity, const std::string& text) {
    char* buffer = reinterpret_cast<char*>(address);
    std::memset(buffer, 0, capacity);
    std::memcpy(buffer, text.data(), std::min(text.size(), capacity - 1));
}

}  // namespace game_text
