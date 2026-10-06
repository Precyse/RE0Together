#pragma once
// Synced cutscenes (docs/CONTRACT.md, "Cutscenes"): the host announces a story Sequence it is about to play (START), each
// guest answers once its own copy is held ready (READY), the host releases both (GO) and tells when it stopped (END).
// The host is the one that decides: only it announces, a guest never plays a story cutscene that was not announced.
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace cutscene_wire {

constexpr uint16_t kMsgStart = proto::kFirstGameType + 0x2A;  // 0x012A, host to all, reliable: Start
constexpr uint16_t kMsgReady = proto::kFirstGameType + 0x2B;  // 0x012B, guest to host, reliable: Ready
constexpr uint16_t kMsgGo = proto::kFirstGameType + 0x2C;     // 0x012C, host to all, reliable: Go
constexpr uint16_t kMsgEnd = proto::kFirstGameType + 0x2D;    // 0x012D, host to all, reliable: End
constexpr size_t kUuidSize = 16;

// ESequenceCategory of the cutscenes that are shared (Story, StoryHolo, StoryForceRadio, StoryPrivateRoomRadio,
// DollmanTalkCutscene); the menu radio is the player's own and stays local.
constexpr uint8_t kCategoryStory = 1, kCategoryStoryHolo = 2, kCategoryForceRadio = 3, kCategoryPrivateRoomRadio = 5,
                  kCategoryDollman = 7;

// How a Sequence stopped, as the engine's stop reason (ESequenceNetworkStopReason).
constexpr uint8_t kStopFinished = 0, kStopScripted = 3, kStopReasonLast = 3;

struct Start {
    uint32_t id;          // the host's number for this playback, echoed by READY, GO and END
    uint8_t category;     // ESequenceCategory
    uint8_t reserved[3];
    int32_t stopFrame;    // the Sequence's end frame (1 s = 120 frames)
    uint8_t resource[kUuidSize];  // the SequenceResource's UUID: what the guest recognises its own copy by
    uint8_t entity[kUuidSize];    // the host's Sequence entity UUID (child Sequences derive theirs from the parent's)
    uint8_t network[kUuidSize];   // the SequenceNetwork that owns it; all zero when the host could not find it
};
static_assert(sizeof(Start) == 60);

struct Ready {
    uint32_t id;
};

struct Go {
    uint32_t id;
};

struct End {
    uint32_t id;
    int32_t frame;     // the host's frame when it stopped: before the stop frame means skipped or cut short
    uint8_t reason;    // the engine's stop reason
    uint8_t reserved[3];
};
static_assert(sizeof(End) == 12);

inline bool isSharedCategory(uint8_t category) {
    return category == kCategoryStory || category == kCategoryStoryHolo || category == kCategoryForceRadio ||
           category == kCategoryPrivateRoomRadio || category == kCategoryDollman;
}

template <class T>
bool decodeFixed(std::span<const uint8_t> payload, T& out) {
    if (payload.size() != sizeof(T)) return false;
    std::memcpy(&out, payload.data(), sizeof(T));
    return true;
}

// False for a payload of the wrong size, id zero or an unshared category.
inline bool decode(std::span<const uint8_t> payload, Start& out) {
    Start start;
    if (!decodeFixed(payload, start) || start.id == 0 || !isSharedCategory(start.category)) return false;
    out = start;
    return true;
}

inline bool decode(std::span<const uint8_t> payload, Ready& out) {
    return decodeFixed(payload, out) && out.id != 0;
}

inline bool decode(std::span<const uint8_t> payload, Go& out) {
    return decodeFixed(payload, out) && out.id != 0;
}

inline bool decode(std::span<const uint8_t> payload, End& out) {
    End end;
    if (!decodeFixed(payload, end) || end.id == 0 || end.reason > kStopReasonLast) return false;
    out = end;
    return true;
}

// Whether the host stopped before the end: the guest cuts its own playback short (a skip, or a stop by script).
inline bool endedEarly(const End& end, int32_t stopFrame, int32_t slackFrames) {
    return end.frame + slackFrames < stopFrame;
}

}  // namespace cutscene_wire
