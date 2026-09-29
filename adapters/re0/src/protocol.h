#pragma once
// Loopback wire format, mirrors launcher/Framing.cs and launcher/ControlMessages.cs.
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace proto {

constexpr uint16_t kVersion = 1;
constexpr uint32_t kMaxFrameLen = 1u << 20;
constexpr size_t kLenFieldSize = 4;
constexpr size_t kHeaderSize = 4;

constexpr uint8_t kFlagReliable = 1;
constexpr uint8_t kSlotAll = 0xFF;

constexpr uint16_t kHello = 0x0001;
constexpr uint16_t kWelcome = 0x0002;
constexpr uint16_t kPeerUp = 0x0003;
constexpr uint16_t kPeerDown = 0x0004;
constexpr uint16_t kReject = 0x0005;
constexpr uint16_t kPeerStats = 0x0012;
constexpr uint16_t kHeartbeat = 0x0020;
constexpr uint16_t kFirstGameType = 0x0100;
constexpr uint16_t kMsgRoomState = kFirstGameType + 4;  // 0x0104
constexpr uint16_t kMsgMenuState = kFirstGameType + 5;  // 0x0105
constexpr uint16_t kMsgInventory = kFirstGameType + 6;  // 0x0106
constexpr uint16_t kMsgFloorPut = kFirstGameType + 7;   // 0x0107
constexpr uint16_t kMsgFloorTake = kFirstGameType + 8;  // 0x0108
constexpr uint16_t kMsgPartyRequest = kFirstGameType + 9;  // 0x0109, guest to host, reliable: empty (toggle the party mode)
constexpr uint16_t kMsgPartyMode = kFirstGameType + 10;    // 0x010A, host to all, reliable: u8 PartyMode
constexpr uint16_t kMsgDoorChange = kFirstGameType + 11;   // 0x010B, focused owner to all, reliable: door_sync::DoorChange
constexpr uint16_t kMsgFlagDiff = kFirstGameType + 12;     // 0x010C, to all, reliable: flag_sync changes

constexpr size_t kWelcomeSize = 7;
constexpr size_t kPeerUpFixedSize = 10;
constexpr size_t kPeerStatsSize = 3;

template <class T>
T readLe(const uint8_t* p) {
    T value;
    std::memcpy(&value, p, sizeof(T));
    return value;
}

inline std::vector<uint8_t> encodeFrame(uint16_t type, uint8_t flags, uint8_t slot,
                                        std::span<const uint8_t> payload) {
    std::vector<uint8_t> out(kLenFieldSize + kHeaderSize + payload.size());
    const uint32_t len = static_cast<uint32_t>(kHeaderSize + payload.size());
    std::memcpy(out.data(), &len, sizeof(len));
    std::memcpy(out.data() + kLenFieldSize, &type, sizeof(type));
    out[kLenFieldSize + 2] = flags;
    out[kLenFieldSize + 3] = slot;
    if (!payload.empty()) std::memcpy(out.data() + kLenFieldSize + kHeaderSize, payload.data(), payload.size());
    return out;
}

}  // namespace proto
