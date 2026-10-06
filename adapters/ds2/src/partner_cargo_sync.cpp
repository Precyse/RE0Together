#include "partner_cargo_sync.h"

#include <algorithm>
#include <chrono>
#include <optional>

#include "cargo_transfer.h"
#include "log.h"
#include "partner_cargo_wire.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;
using partner_cargo_wire::Moved;

constexpr auto kAckGiveUp = std::chrono::seconds(15);  // a move this machine could not carry out is acknowledged anyway

// A move this machine made on request of the partner, acknowledged once the local list shows it.
struct Awaiting {
    Moved moved;
    game::Cargo piece;
    Clock::time_point since;
};

// Net thread only.
std::vector<game::Cargo> g_left, g_given;  // this machine's moves the partner has not acknowledged
std::vector<Awaiting> g_awaiting;
uint8_t g_partner = proto::kSlotAll;
bool g_host = false;

// Deletes this machine's copy of the piece of the same order (plain cargo: the same kind); the piece it deleted, if any.
std::optional<game::Cargo> deleteOwnCopy(const partner_cargo_wire::CargoGone& gone) {
    for (const game::Cargo& piece : game::carriedCargo()) {
        const bool match = gone.orderId ? piece.orderId == gone.orderId : (piece.orderId == 0 && piece.type == gone.type);
        if (match && game::removeCargo(piece.handle)) {
            logger::write("partner_cargo_sync: deleted my copy of the piece the partner took or delivered (kind %u)", gone.type);
            return piece;
        }
    }
    logger::write("partner_cargo_sync: no copy of the moved piece (kind %u, order %llx) here", gone.type,
                  static_cast<unsigned long long>(gone.orderId));
    return std::nullopt;
}

bool sameKey(const game::Cargo& a, const game::Cargo& b) { return a.type == b.type && a.orderId == b.orderId; }

// Whether the local CARGO_LIST (which is sent in the same tick as it is refreshed) no longer holds or now holds the piece.
bool shown(const Awaiting& waiting) {
    const std::vector<game::Cargo> list = cargo_transfer::localCargo();
    const bool present = std::any_of(list.begin(), list.end(), [&](const game::Cargo& piece) {
        return waiting.moved == Moved::Gone ? piece.handle == waiting.piece.handle : sameKey(piece, waiting.piece);
    });
    return present == (waiting.moved == Moved::Added);
}

void sendAck(NetClient& net, uint8_t partner, const Awaiting& waiting) {
    const partner_cargo_wire::MoveAck ack{waiting.piece.type, waiting.moved, waiting.piece.orderId};
    if (!net.send(partner_cargo_wire::kMsgMoveAck, true, partner, proto::bytesOf(ack))) {
        logger::write("partner_cargo_sync: could not send MOVE_ACK");
    }
}

void acknowledgeShownMoves(NetClient& net, uint8_t partner, Clock::time_point now) {
    std::erase_if(g_awaiting, [&](const Awaiting& waiting) {
        if (!shown(waiting) && now - waiting.since < kAckGiveUp) return false;
        sendAck(net, partner, waiting);
        return true;
    });
}

void release(std::vector<game::Cargo>& held, const partner_cargo_wire::MoveAck& ack) {
    const auto at = std::find_if(held.begin(), held.end(),
                                 [&](const game::Cargo& piece) { return piece.type == ack.type && piece.orderId == ack.orderId; });
    if (at != held.end()) held.erase(at);
}

void sendLeft(NetClient& net, uint8_t partner, const std::vector<game::Cargo>& left) {
    for (const game::Cargo& piece : left) {
        const partner_cargo_wire::CargoGone gone{piece.type, 0, piece.orderId};
        if (net.send(partner_cargo_wire::kMsgCargoGone, true, partner, proto::bytesOf(gone))) {
            g_left.push_back(piece);
        } else {
            logger::write("partner_cargo_sync: could not send CARGO_GONE");
        }
    }
}

// Only the host decides what moves between the players' racks, so only its gives are carried out.
void sendGiven(NetClient& net, uint8_t partner, const std::vector<game::Cargo>& given) {
    for (const game::Cargo& piece : given) {
        if (!g_host) {
            logger::write("partner_cargo_sync: a guest's give into the partner's rack is not carried out (kind %u)", piece.type);
            continue;
        }
        cargo_transfer::sendPiece(net, partner, piece);
        g_given.push_back(piece);
    }
}

}  // namespace

namespace partner_cargo_sync {

void onFrame(const GameFrame& frame) {
    if (frame.slot != remote_body::slot()) return;
    if (frame.type == partner_cargo_wire::kMsgCargoGone) {
        partner_cargo_wire::CargoGone gone;
        if (!partner_cargo_wire::decode(frame.payload, gone)) {
            logger::write("partner_cargo_sync: dropped a malformed CARGO_GONE (%zu bytes)", frame.payload.size());
            return;
        }
        const std::optional<game::Cargo> removed = deleteOwnCopy(gone);
        const game::Cargo piece = removed ? *removed : game::Cargo{0, gone.type, {}, gone.orderId};
        g_awaiting.push_back({Moved::Gone, piece, Clock::now()});
    } else if (frame.type == partner_cargo_wire::kMsgMoveAck) {
        partner_cargo_wire::MoveAck ack;
        if (partner_cargo_wire::decode(frame.payload, ack)) release(ack.moved == Moved::Gone ? g_left : g_given, ack);
    }
}

void expectAdded(const game::Cargo& piece) { g_awaiting.push_back({Moved::Added, piece, Clock::now()}); }

void tick(NetClient& net, const SessionSnapshot& session) {
    g_host = session.linked && session.localSlot == session.hostSlot;
    const uint8_t partner = remote_body::slot();
    if (!session.linked || partner != g_partner) {
        g_left.clear();
        g_given.clear();
        g_awaiting.clear();
    }
    g_partner = session.linked ? partner : proto::kSlotAll;
    const game::PartnerMoves moves = game::takePartnerMoves();
    if (g_partner == proto::kSlotAll) return;
    sendLeft(net, partner, moves.left);
    sendGiven(net, partner, moves.given);
    acknowledgeShownMoves(net, partner, Clock::now());
}

std::vector<game::Cargo> recentlyLeft() { return g_left; }

std::vector<game::Cargo> recentlyGiven() { return g_given; }

}  // namespace partner_cargo_sync
