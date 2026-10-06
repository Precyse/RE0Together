#pragma once
// The guest's rule for a host mission event (ds2/story.cpp replays it through the game's own request calls): a start is
// replayed only for a mission that has not started here, a success or failure only for one in progress, so a repeated or
// late event (the host's request and its state poll both report a change) is a no-op.
#include <cstdint>

#include "story_wire.h"

namespace story_replay {

constexpr uint16_t kStateProgress = 20;  // EDSMissionState: in progress (30 failed, 40 success follow it)

inline bool isStart(story_wire::Kind kind) { return kind == story_wire::Kind::MissionStart || kind == story_wire::Kind::OrderRequest; }

inline bool applies(story_wire::Kind kind, uint16_t missionState) {
    return isStart(kind) ? missionState < kStateProgress : missionState == kStateProgress;
}

}  // namespace story_replay
