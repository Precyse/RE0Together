#include "partner_cargo_sync.h"

#include <algorithm>
#include <chrono>

#include "cargo_transfer.h"
#include "log.h"
#include "partner_cargo_wire.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kHold = std::chrono::seconds(10);  // longer than the partner's list takes to show a move

struct Held {
    game::Cargo piece;
    Clock::time_point at;
};

// Net thread only.
std::vector<Held> g_left, g_given;
bool g_host = false;

// The piece of the same order (plain cargo: the same kind) that this machine's player carries.
void deleteOwnCopy(const partner_cargo_wire::CargoGone& gone) {
    for (const game::Cargo& piece : game::carriedCargo()) {
        const bool match = gone.orderId ? piece.orderId == gone.orderId : (piece.orderId == 0 && piece.type == gone.type);
        if (match && game::removeCargo(piece.handle)) {
            logger::write("partner_cargo_sync: deleted my copy of the piece the partner took or delivered (kind %u)", gone.type);
            return;
        }
    }
    logger::write("partner_cargo_sync: no copy of the moved piece (kind %u, order %llx) here", gone.type,
                  static_cast<unsigned long long>(gone.orderId));
}

void expire(std::vector<Held>& held, Clock::time_point now) {
    std::erase_if(held, [now](const Held& entry) { return now - entry.at > kHold; });
}

std::vector<game::Cargo> pieces(const std::vector<Held>& held) {
    std::vector<game::Cargo> out;
    for (const Held& entry : held) out.push_back(entry.piece);
    return out;
}

void sendLeft(NetClient& net, uint8_t partner, const std::vector<game::Cargo>& left, Clock::time_point now) {
    for (const game::Cargo& piece : left) {
        const partner_cargo_wire::CargoGone gone{piece.type, 0, piece.orderId};
        if (!net.send(partner_cargo_wire::kMsgCargoGone, true, partner, proto::bytesOf(gone))) {
            logger::write("partner_cargo_sync: could not send CARGO_GONE");
        }
        g_left.push_back({piece, now});
    }
}

// Only the host decides what moves between the players' racks, so only its gives are carried out.
void sendGiven(NetClient& net, uint8_t partner, const std::vector<game::Cargo>& given, Clock::time_point now) {
    for (const game::Cargo& piece : given) {
        if (!g_host) {
            logger::write("partner_cargo_sync: a guest's give into the partner's rack is not carried out (kind %u)", piece.type);
            continue;
        }
        cargo_transfer::sendPiece(net, partner, piece);
        g_given.push_back({piece, now});
    }
}

}  // namespace

namespace partner_cargo_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != partner_cargo_wire::kMsgCargoGone || frame.slot != remote_body::slot()) return;
    partner_cargo_wire::CargoGone gone;
    if (partner_cargo_wire::decode(frame.payload, gone)) {
        deleteOwnCopy(gone);
    } else {
        logger::write("partner_cargo_sync: dropped a malformed CARGO_GONE (%zu bytes)", frame.payload.size());
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    g_host = session.linked && session.localSlot == session.hostSlot;
    const auto now = Clock::now();
    expire(g_left, now);
    expire(g_given, now);
    const game::PartnerMoves moves = game::takePartnerMoves();
    const uint8_t partner = remote_body::slot();
    if (!session.linked || partner == proto::kSlotAll) return;
    sendLeft(net, partner, moves.left, now);
    sendGiven(net, partner, moves.given, now);
}

std::vector<game::Cargo> recentlyLeft() { return pieces(g_left); }

std::vector<game::Cargo> recentlyGiven() { return pieces(g_given); }

}  // namespace partner_cargo_sync
