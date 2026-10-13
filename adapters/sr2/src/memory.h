#pragma once
#include <cstddef>
#include <cstdint>

// Copies `size` bytes of this process at `address` into `out`; false when the range is not readable.
bool readMemory(uintptr_t address, void* out, size_t size);
