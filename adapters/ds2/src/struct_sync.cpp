#include "struct_sync.h"

#include "game.h"
#include "log.h"
#include "struct_wire.h"

namespace {

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;

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
    if (!host) return;
    for (const struct_wire::Placed& placed : game::takePlacedStructures()) {
        if (!net.send(struct_wire::kMsgStructCreate, true, proto::kSlotAll, struct_wire::encode(placed))) {
            logger::write("struct_sync: could not send structure %u", placed.create.id);
        }
    }
    for (const struct_wire::Remove& removal : game::takeRemovedStructures()) {
        net.send(struct_wire::kMsgStructRemove, true, proto::kSlotAll, proto::bytesOf(removal));
    }
}

}  // namespace struct_sync
