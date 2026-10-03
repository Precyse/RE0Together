#pragma once
// The host's story, mirrored to the guests (STORY_EVENT, story_wire.h): missions started, succeeded or failed and story
// sections switched, replayed on a guest through the game's own request calls while its own story requests are vetoed.
#include "net_client.h"

namespace story_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace story_sync
