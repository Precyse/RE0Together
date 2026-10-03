#pragma once
// STORY_EVENT: what the host's story did (a mission started, succeeded or failed, a story section switched), sent to
// the guests so they replay it through the game's own request calls. The host is the world server: the guest's own
// story requests are vetoed (docs/DS2_NOTES.md, "Story sync").
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace story_wire {

constexpr uint16_t kMsgStoryEvent = proto::kFirstGameType + 0x19;  // 0x0119, host to all, reliable: Event
constexpr size_t kUuidSize = 16;

enum class Kind : uint8_t { MissionStart = 1, MissionSuccess = 2, MissionFail = 3, SectionActive = 4, SectionInactive = 5 };

struct Event {
    uint8_t kind;
    uint8_t reserved[3];
    uint32_t a;                  // mission start: the request's first argument; success: the flag; fail: the reason
    int32_t b;                   // mission start: the request's second argument
    uint32_t reserved2;
    uint64_t missionId;          // mission events
    uint8_t section[kUuidSize];  // section events: the section's UUID
};
static_assert(sizeof(Event) == 40);

inline bool isMission(Kind k) { return k == Kind::MissionStart || k == Kind::MissionSuccess || k == Kind::MissionFail; }
inline bool isSection(Kind k) { return k == Kind::SectionActive || k == Kind::SectionInactive; }

// False for a payload of the wrong size or an unknown kind (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, Event& out) {
    if (payload.size() != sizeof(Event)) return false;
    Event event;
    std::memcpy(&event, payload.data(), sizeof(event));
    const auto kind = static_cast<Kind>(event.kind);
    if (!isMission(kind) && !isSection(kind)) return false;
    out = event;
    return true;
}

}  // namespace story_wire
