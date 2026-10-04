#include "struct_sync.h"

#include <map>

#include "game.h"
#include "log.h"
#include "resync.h"
#include "struct_wire.h"

namespace {

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
bool g_requestedAtGameplay = false;
size_t g_knownPeers = 0;
std::map<uint32_t, struct_wire::Placed> g_live;  // host: the structures it placed this session, by construction id

void sendCreate(NetClient& net, const struct_wire::Placed& placed) {
    if (!net.send(struct_wire::kMsgStructCreate, true, proto::kSlotAll, struct_wire::encode(placed))) {
        logger::write("struct_sync: could not send structure %u", placed.create.id);
    }
}

}  // namespace

namespace struct_sync {

void onFrame(const GameFrame& frame) {
    if (!g_guest || frame.slot != g_hostSlot) return;
    if (frame.type == struct_wire::kMsgStructCreate) {
        struct_wire::Placed placed;
        if (struct_wire::decode(frame.payload, placed)) {
            game::buildStructure(placed);
        } else {
            logger::write("struct_sync: dropped a malformed STRUCT_CREATE (%zu bytes)", frame.payload.size());
        }
    } else if (frame.type == struct_wire::kMsgStructRemove) {
        struct_wire::Remove removal;
        if (struct_wire::decode(frame.payload, removal)) {
            game::removeStructure(removal);
        } else {
            logger::write("struct_sync: dropped a malformed STRUCT_REMOVE (%zu bytes)", frame.payload.size());
        }
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    game::setStructureRole(host, g_guest);
    if (g_guest) {
        if (!game::gameplaySettled()) {
            g_requestedAtGameplay = false;
        } else if (!g_requestedAtGameplay && resync::request(net, g_hostSlot, resync::kStructures)) {
            g_requestedAtGameplay = true;
            logger::write("struct_sync: gameplay started, asked the host for the structures it placed");
        }
    } else {
        g_requestedAtGameplay = false;
    }
    if (!host) {
        g_live.clear();
        return;
    }
    // A peer that joined, or asked, gets every structure placed so far (a guest skips the ones it already has).
    if (session.peers.size() > g_knownPeers || !resync::takeRequests(resync::kStructures).empty()) {
        for (const auto& [id, placed] : g_live) sendCreate(net, placed);
    }
    g_knownPeers = session.peers.size();
    for (const struct_wire::Placed& placed : game::takePlacedStructures()) {
        g_live[placed.create.id] = placed;
        sendCreate(net, placed);
    }
    for (const struct_wire::Remove& removal : game::takeRemovedStructures()) {
        g_live.erase(removal.id);
        net.send(struct_wire::kMsgStructRemove, true, proto::kSlotAll, proto::bytesOf(removal));
    }
}

}  // namespace struct_sync
