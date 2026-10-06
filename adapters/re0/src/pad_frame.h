#pragma once
// PAD_FRAME (0x0101): one frame of the controlled player's input, as answered by the game's pad object.
#include <cstdint>

#include "protocol.h"

namespace pad {

constexpr uint16_t kMsgPadFrame = proto::kFirstGameType + 1;

// Pad object vtable layout: slots 5..35 are input queries.
constexpr size_t kVtableSlotCount = 36;
constexpr size_t kFirstQuerySlot = 5;
constexpr size_t kQuerySlotCount = 31;
constexpr size_t kStickSlot = 14;  // void* f(void* out16)
constexpr size_t kArgSlot = 15;    // u32 f(u32 arg)
constexpr size_t kActionSlot = 18;  // "action pressed" (+0x48): doors, items and every other interaction

constexpr size_t kStickBytes = 16;
constexpr size_t kAnalogBytes = 0x40;
constexpr size_t kFramesPerPacket = 3;

struct PadFrame {
    uint32_t frame;
    uint32_t values[kQuerySlotCount];  // indexed by slot - kFirstQuerySlot
    uint8_t stick[kStickBytes];
    uint8_t analog[kAnalogBytes];
};
static_assert(sizeof(PadFrame) == 208);

// Wire payload: the newest frames, oldest first, so a lost packet is covered by the next ones.
struct PadPacket {
    uint32_t count;
    PadFrame frames[kFramesPerPacket];
};

}  // namespace pad
