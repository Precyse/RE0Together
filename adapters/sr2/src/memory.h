#pragma once
#include <cstddef>
#include <cstdint>

// Copies `size` bytes of this process at `address` into `out`; false when the range is not readable.
bool readMemory(uintptr_t address, void* out, size_t size);

// The 32-bit field at `base + offset` of this process, for static game objects that are always mapped.
inline uint32_t& dwordField(uintptr_t base, uintptr_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

// The byte at `base + offset` of this process, for static game objects that are always mapped.
inline uint8_t& byteField(uintptr_t base, uintptr_t offset) { return *reinterpret_cast<uint8_t*>(base + offset); }

// The byte at `address` in this process, 0 when it is not readable.
inline uint8_t readByte(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// The 32-bit value at `address` in this process, 0 when it is not readable.
inline uint32_t readDword(uintptr_t address) {
    uint32_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// The NUL-terminated text at `address` copied into `out`, up to `capacity` - 1 characters, then a NUL; the number of characters
// copied. Stops early at an unreadable byte.
inline size_t readCString(uintptr_t address, char* out, size_t capacity) {
    size_t length = 0;
    while (length + 1 < capacity && readMemory(address + length, &out[length], 1) && out[length] != '\0') ++length;
    out[length] = '\0';
    return length;
}
