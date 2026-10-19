#pragma once
#include <cstdint>

// Who may send a state-mode message (docs/RE0_NOTES.md "State mode: plumbing"). Pure, no game access.
namespace frame_router_rule {

enum class SenderPolicy : uint8_t {
    Any,       // any peer
    HostOnly,  // the sender must be the host (commits, JOIN_EXTRAS)
    GuestOnly  // a guest to the host: this machine must be the host and the sender a guest (requests, proposals)
};

constexpr bool admits(SenderPolicy policy, uint8_t sender, uint8_t host, bool localIsHost) {
    switch (policy) {
        case SenderPolicy::Any: return true;
        case SenderPolicy::HostOnly: return sender == host;
        case SenderPolicy::GuestOnly: return localIsHost && sender != host;
    }
    return false;
}

// The reason shown when a frame is dropped.
constexpr const char* name(SenderPolicy policy) {
    switch (policy) {
        case SenderPolicy::Any: return "any sender";
        case SenderPolicy::HostOnly: return "host only";
        case SenderPolicy::GuestOnly: return "guest only";
    }
    return "unknown";
}

}  // namespace frame_router_rule
