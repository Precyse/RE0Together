#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// What a join_req carries and a join_deny says (pure, unit tested). The password is the first field of a join_req
// payload (docs/SR2_NOTES.md "Version gate", "Force join"); a join_deny carries a u32 game version, then the reason.
namespace join_deny_rules {

constexpr uint8_t kReasonNone = 0;
constexpr uint8_t kReasonWrongVersion = 1;  // the game's own "wrong game version"
constexpr uint8_t kReasonBadPassword = 7;   // shown as MULTI_BAD_PASSWORD
constexpr size_t kReasonOffset = 4;         // in the join_deny payload
constexpr size_t kMaxPasswordBytes = 64;    // the field, its end mark included

// True when the payload starts with exactly `secret` and an end mark inside the field.
inline bool passwordMatches(const uint8_t* payload, size_t available, const std::string& secret) {
    const size_t searchable = available < kMaxPasswordBytes ? available : kMaxPasswordBytes;
    const void* mark = memchr(payload, 0, searchable);
    if (!mark) return false;
    const size_t length = static_cast<const uint8_t*>(mark) - payload;
    return length == secret.size() && memcmp(payload, secret.data(), length) == 0;
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
