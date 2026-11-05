#pragma once
// The shared story (cd design progression.md 4.2-4.3; static_s14_s16.md). The host logs every stage
// branch its own game chose and its server applied (its StageBranchStateReq, then the Ack in apply mode for that
// instance and branch) under the stage's sequencer key (from its PrepareSequencerAck) and sends each as STAGE_BRANCH,
// the whole log again to every joiner. A guest keeps its own key-to-instance map the same way and queues the host's
// entries (story_rule.h). With story_replay on, an entry whose stage our world has, that is not busy and whose branch
// the guard finds in the stage's table (stage_guard.h) is posted as our own StageBranchStateReq and dropped once our
// server's Ack applies it; otherwise each entry is only logged. The 30 s line checks the Ack's field offsets live.
#include <cstdint>
#include <span>

#include "funnels.h"

namespace story {

// After the catalogue: the stage and sequencer messages. False when a name is missing.
bool resolveOpcodes();

// From adapter.ini story_replay (a guest posts guarded branches; otherwise it only logs them).
void setReplay(bool on);

// Main tick.
void onEvent(const funnels::Event& event, uint64_t nowMs);
void tick(uint64_t nowMs, bool peerJoined);

// Link routing: STAGE_BRANCH from the host. False for any other type or a payload that does not decode.
bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload);

// A replayed branch is posted and our server has not answered it yet (save_guard holds autosaves meanwhile).
bool replayWaiting();

// Gives up the replays still waiting (the save hold reached its bound); their entries stay queued.
void abandonReplays();

struct Stats {
    uint32_t logged = 0;      // host: entries appended
    uint32_t received = 0;    // guest: entries queued
    uint32_t wouldApply = 0;  // guest: entries the replay reached (logged once each)
    uint32_t posted = 0;      // guest: replayed Reqs posted
    uint32_t applied = 0;     // guest: replays our server applied
};
Stats stats();

}  // namespace story
