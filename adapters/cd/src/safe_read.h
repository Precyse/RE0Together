#pragma once
// SEH-guarded reads of game memory: a bad pointer gives false instead of a crash.
#include <cstddef>
#include <cstdint>

namespace safe_read {

bool bytes(uintptr_t address, void* out, size_t size);

template <typename T>
bool value(uintptr_t address, T& out) {
    return bytes(address, &out, sizeof(T));
}

// The pointer stored at `address`, or 0 when it cannot be read.
inline uintptr_t pointer(uintptr_t address) {
    uintptr_t out = 0;
    return value(address, out) ? out : 0;
}

}  // namespace safe_read
