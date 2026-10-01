#pragma once
// Reads of game memory that may be freed or half-built under us: a fault returns false instead of crashing.
#include <cstddef>
#include <cstdint>

namespace decima {

bool safeCopy(void* out, uintptr_t address, size_t size);

template <class T>
bool safeRead(uintptr_t address, T& out) {
    return address && safeCopy(&out, address, sizeof(T));
}

inline uintptr_t readPointer(uintptr_t address) {
    uintptr_t value = 0;
    return safeRead(address, value) ? value : 0;
}

}  // namespace decima
