#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "event_sync.h"
#include "protocol.h"

// Wire format of the shared cutscene skip (partner_mode=state, docs/RE0_NOTES.md "State mode: scripts and
// cutscenes"). The thread's leader decides: a press on the follower goes to the leader as DEMO_SKIP_REQUEST, the leader's
// scheduler skips and DEMO_SKIP tells the follower to take the same skip. Pure, no game access, unit tested.
namespace demo_skip_wire {

constexpr uint16_t kMsgDemoSkipRequest = proto::kFirstGameType + 0x53;  // 0x0153, follower to leader, reliable
constexpr uint16_t kMsgDemoSkip = proto::kFirstGameType + 0x54;         // 0x0154, leader to follower, reliable

// Demo ids are the S2 of Demo_Start; the demo scenes are 0xaf + id (event_rule.h), below the 0x100 scene limit.
constexpr uint16_t kDemoIdLimit = 0x100;

// Both messages: the demo and the thread that started it. `flags` is event_sync::kSerialIsReceivers when the serial was
// allocated by the receiver, as in EVENT_STEPS.
struct DemoSkip {
    uint16_t demoId;
    uint16_t serial;
    uint8_t flags;
    uint8_t reserved;
};
static_assert(sizeof(DemoSkip) == 6);

constexpr uint8_t flagsFor(bool serialMine) { return serialMine ? 0 : event_sync::kSerialIsReceivers; }

// A DemoSkip of the right size for a demo and a thread the sender can have.
inline std::optional<DemoSkip> decode(std::span<const uint8_t> bytes) {
    if (bytes.size() != sizeof(DemoSkip)) return std::nullopt;
    DemoSkip skip;
    std::memcpy(&skip, bytes.data(), sizeof(skip));
    if (skip.demoId >= kDemoIdLimit || skip.serial == 0 || skip.flags > event_sync::kSerialIsReceivers ||
        skip.reserved != 0) {
        return std::nullopt;
    }
    return skip;
}

}  // namespace demo_skip_wire
