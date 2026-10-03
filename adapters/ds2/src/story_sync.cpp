#include "story_sync.h"

#include "game.h"
#include "log.h"
#include "resync.h"
#include "story_wire.h"

namespace {

// Net thread only.
bool g_guest = false;
bool g_requestedAtGameplay = false;
uint8_t g_hostSlot = 0;
size_t g_knownPeers = 0;

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
    if (g_guest) {
        if (!game::gameplaySettled()) {
            g_requestedAtGameplay = false;
        } else if (!g_requestedAtGameplay && resync::request(net, g_hostSlot, resync::kStory)) {
            g_requestedAtGameplay = true;
        }
    } else {
        g_requestedAtGameplay = false;
    }
    if (!host) return;
    if (session.peers.size() > g_knownPeers || !resync::takeRequests(resync::kStory).empty()) game::requestStorySnapshot();
    g_knownPeers = session.peers.size();
    for (const story_wire::Event& event : game::takeStoryEvents()) {
        if (!net.send(story_wire::kMsgStoryEvent, true, proto::kSlotAll, proto::bytesOf(event))) {
            logger::write("story_sync: could not send a story event");
        }
    }
}

}  // namespace story_sync
