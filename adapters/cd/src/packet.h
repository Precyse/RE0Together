#pragma once
// The Troc packet as it passes the client funnels (pure): u16 opcode, u8 slot, u16 payload length, then the payload.
#include <cstdint>
#include <cstring>

namespace packet {

constexpr uint16_t kHeaderSize = 5;

enum class Direction : uint8_t { ClientToServer = 0, ServerToClient = 1 };

struct Header {
    uint16_t opcode = 0;
    uint8_t slot = 0;
    uint16_t payloadSize = 0;
};

// False when `size` cannot hold the header or the header's payload length disagrees with `size`.
inline bool parse(const uint8_t* bytes, uint32_t size, Header& out) {
    if (!bytes || size < kHeaderSize) return false;
    std::memcpy(&out.opcode, bytes, sizeof(out.opcode));
    out.slot = bytes[2];
    std::memcpy(&out.payloadSize, bytes + 3, sizeof(out.payloadSize));
    return static_cast<uint32_t>(out.payloadSize) + kHeaderSize == size;
}

}  // namespace packet
