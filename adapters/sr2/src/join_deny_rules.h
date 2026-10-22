#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "game_message.h"

// What a join_req carries and a join_deny says (pure, unit tested). The password is the first field of a join_req
// payload (docs/SR2_NOTES.md "Version gate", "Force join"); a join_deny carries a u32 game version, then the reason, then
// the host's version gate trailer.
namespace join_deny_rules {

constexpr uint8_t kReasonNone = 0;
constexpr uint8_t kReasonWrongVersion = 1;  // the game's own "wrong game version"
constexpr uint8_t kReasonBadPassword = 7;   // shown as MULTI_BAD_PASSWORD
constexpr size_t kReasonOffset = 4;         // in the join_deny payload
constexpr size_t kMaxPasswordBytes = 64;    // the field, its end mark included
constexpr uint8_t kDenyMessageId = 4;       // join_deny in the transport's message table
constexpr uint32_t kGameVersion = 0xC9;     // the first field of a join_deny
constexpr size_t kMaxDenyBytes = 64;

// True when the payload starts with exactly `secret` and an end mark inside the field.
inline bool passwordMatches(const uint8_t* payload, size_t available, const std::string& secret) {
    const size_t searchable = available < kMaxPasswordBytes ? available : kMaxPasswordBytes;
    const void* mark = memchr(payload, 0, searchable);
    if (!mark) return false;
    const size_t length = static_cast<const uint8_t*>(mark) - payload;
    return length == secret.size() && memcmp(payload, secret.data(), length) == 0;
}

// A join_deny ready for the game's send to an address: the bytes, and how many of them.
struct DenyMessage {
    std::array<uint8_t, kMaxDenyBytes> bytes{};
    size_t length = 0;
};

// As the game's own sender 0x816F60 builds it (3 byte header): id 4, the u16 payload length, the u32 game version, the
// reason byte, and then `trailer` (the host's version gate trailer, which the guest compares with its own).
inline DenyMessage denyMessage(uint8_t reason, const uint8_t* trailer, size_t trailerBytes) {
    DenyMessage message;
    const size_t payload = sizeof(kGameVersion) + sizeof(reason) + trailerBytes;
    message.bytes[0] = kDenyMessageId;
    message.bytes[1] = static_cast<uint8_t>(payload);
    message.bytes[2] = static_cast<uint8_t>(payload >> 8);
    memcpy(message.bytes.data() + game_message::kHeaderBytes, &kGameVersion, sizeof(kGameVersion));
    message.bytes[game_message::kHeaderBytes + sizeof(kGameVersion)] = reason;
    memcpy(message.bytes.data() + game_message::kHeaderBytes + sizeof(kGameVersion) + sizeof(reason), trailer, trailerBytes);
    message.length = game_message::kHeaderBytes + payload;
    return message;
}

// The word the guest's status gives when a refusal ends the join for good, or none when another attempt may work.
inline const char* finalDetail(uint8_t reason) {
    switch (reason) {
        case kReasonWrongVersion: return "version";
        case kReasonBadPassword: return "password";
        default: return nullptr;
    }
}

}  // namespace join_deny_rules
