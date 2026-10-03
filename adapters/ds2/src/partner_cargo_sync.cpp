#include "partner_cargo_sync.h"

#include "game.h"
#include "log.h"
#include "partner_cargo_wire.h"

namespace {

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;

// The piece of the same order (plain cargo: the same kind) that this machine's player carries.
void deleteOwnCopy(const partner_cargo_wire::CargoGone& gone) {
    for (const game::Cargo& piece : game::carriedCargo()) {
        const bool match = gone.orderId ? piece.orderId == gone.orderId : (piece.orderId == 0 && piece.type == gone.type);
        if (match && game::removeCargo(piece.handle)) {
            logger::write("partner_cargo_sync: deleted my copy of the piece the host delivered (kind %u)", gone.type);
            return;
        }
    }
    logger::write("partner_cargo_sync: no copy of the delivered piece (kind %u, order %llx) here", gone.type,
                  static_cast<unsigned long long>(gone.orderId));
}

}  // namespace

namespace partner_cargo_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != partner_cargo_wire::kMsgCargoGone || !g_guest || frame.slot != g_hostSlot) return;
    partner_cargo_wire::CargoGone gone;
    if (partner_cargo_wire::decode(frame.payload, gone)) {
        deleteOwnCopy(gone);
    } else {
        logger::write("partner_cargo_sync: dropped a malformed CARGO_GONE (%zu bytes)", frame.payload.size());
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    const std::vector<game::Cargo> delivered = game::takeDeliveredByPartner();
    if (!host) return;
    for (const game::Cargo& piece : delivered) {
        const partner_cargo_wire::CargoGone gone{piece.type, 0, piece.orderId};
        if (!net.send(partner_cargo_wire::kMsgCargoGone, true, proto::kSlotAll, proto::bytesOf(gone))) {
            logger::write("partner_cargo_sync: could not send CARGO_GONE");
        }
    }
}

}  // namespace partner_cargo_sync
