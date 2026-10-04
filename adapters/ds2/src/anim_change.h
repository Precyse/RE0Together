#pragma once
// One animation variable as the adapter moves it around: its index in the animation manager's table (the same on both
// machines), the engine's variable type and its value. Pure data, shared by the wire codec and the engine side.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace remote_animation {

constexpr uint8_t kTypeBool = 0;
constexpr uint8_t kTypeInt = 1;
constexpr uint8_t kTypeFloat = 2;
constexpr uint8_t kTypeQuat = 3;
constexpr size_t kMaxValueBytes = 16;

struct Change {
    uint16_t index;
    uint8_t type;
    uint8_t value[kMaxValueBytes];  // 1, 4 or 16 bytes by the type
};

constexpr size_t kMaxVariables = 1024;

// The newest value of each variable of one animation manager: what was last sent for it, or what its source last
// reported.
struct VariableValues {
    std::vector<Change> change = std::vector<Change>(kMaxVariables);
    std::vector<uint8_t> valid = std::vector<uint8_t>(kMaxVariables, 0);

    void set(const Change& value) {
        if (value.index >= kMaxVariables) return;
        change[value.index] = value;
        valid[value.index] = 1;
    }
};

// Bytes of a value of that type, 0 for a type that is not mirrored.
inline size_t valueBytes(uint8_t type) {
    switch (type) {
        case kTypeBool:
            return 1;
        case kTypeInt:
        case kTypeFloat:
            return 4;
        case kTypeQuat:
            return 16;
        default:
            return 0;
    }
}

}  // namespace remote_animation
