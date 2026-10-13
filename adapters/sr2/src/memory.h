#pragma once
#include <cstddef>
#include <cstdint>

// Copies `size` bytes of this process at `address` into `out`; false when the range is not readable.
bool readMemory(uintptr_t address, void* out, size_t size);

// The 32-bit field at `base + offset` of this process, for static game objects that are always mapped.
inline uint32_t& dwordField(uintptr_t base, uintptr_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }
