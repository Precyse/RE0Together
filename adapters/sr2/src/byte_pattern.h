#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

// Index of the first byte where `actual` differs from `expected`, or nothing when the `size` bytes are equal.
inline std::optional<size_t> firstMismatch(const uint8_t* actual, const uint8_t* expected, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (actual[i] != expected[i]) return i;
    }
    return std::nullopt;
}
