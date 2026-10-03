#include "story_sync.h"

#include "game.h"
#include "log.h"
#include "story_wire.h"

namespace {

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;

}  // namespace

namespace story_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != story_wire::kMsgStoryEvent || !g_guest || frame.slot != g_hostSlot) return;
    story_wire::Event event;
    if (story_wire::decode(frame.payload, event)) {
        game::replayStoryEvent(event);
    } else {
        logger::write("story_sync: dropped a malformed STORY_EVENT (%zu bytes)", frame.payload.size());
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    game::setStoryRole(host, g_guest);
    if (!host) return;
    for (const story_wire::Event& event : game::takeStoryEvents()) {
        if (!net.send(story_wire::kMsgStoryEvent, true, proto::kSlotAll, proto::bytesOf(event))) {
            logger::write("story_sync: could not send a story event");
        }
    }
}

}  // namespace story_sync
