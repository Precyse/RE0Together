#pragma once
// Actor keys (pure): bit 31 valid, bit 30 client-local, bits 28-29 class, 20-27 generation, 0-19 index (cd design,
// adapter.md 9). Class tests always use the class bits: keys reach 0xB02... after about 15,500 allocations.
#include <cstdint>

namespace actor_key {

enum class Class : uint8_t { None = 0, User = 1, Player = 2, World = 3 };

constexpr uint32_t kValidBit = 0x80000000u;
constexpr uint32_t kClassShift = 28;
constexpr uint32_t kClassMask = 0x3;
constexpr uint32_t kIndexMask = 0xFFFFF;

constexpr bool valid(uint32_t key) { return (key & kValidBit) != 0; }

constexpr Class classOf(uint32_t key) {
    return valid(key) ? static_cast<Class>((key >> kClassShift) & kClassMask) : Class::None;
}

constexpr uint32_t indexOf(uint32_t key) { return key & kIndexMask; }

}  // namespace actor_key
