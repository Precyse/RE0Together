#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Byte writers shared by the generators of x86 thunks.
namespace thunk {

inline void emit(uint8_t*& p, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) *p++ = b;
}

inline void emitAddress(uint8_t*& p, const volatile void* target) {
    const uint32_t value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
    std::memcpy(p, &value, sizeof(value));
    p += sizeof(value);
}

}  // namespace thunk
