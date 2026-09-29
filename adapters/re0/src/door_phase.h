#pragma once
#include <cstdint>

// The sDoorLoad phase (+0x44) is 0, 1, 2, 4 while a door transition runs and 5 when idle; after a fresh save load
// it is 0xFFFFFFFF (-1). Only the running phases count as a door, so both idle values are recognised.
namespace door_phase {

constexpr int32_t kFirst = 0;
constexpr int32_t kLast = 4;
constexpr int32_t kUnreadable = -1;

constexpr bool running(int32_t phase) { return phase >= kFirst && phase <= kLast; }

}  // namespace door_phase
