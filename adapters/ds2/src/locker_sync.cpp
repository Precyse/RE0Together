#include "locker_sync.h"

#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "cargo_transfer.h"
#include "ds2/lockers.h"
#include "game.h"
#include "locker_table.h"
#include "locker_wire.h"
#include "log.h"
#include "peer_joins.h"
#include "reject_counters.h"
#include "resync.h"
#include "time_us.h"

namespace {

using lockers::Locker;
using reject_counters::Reason;

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
PeerJoins g_joins;                                        // host: which peer joins are owed the whole set
std::set<uint8_t> g_owed;                                 // host: slots waiting for the first read to send the whole set
std::map<uint8_t, locker_table::MoveLimiter> g_limiters;  // host: per sending guest
bool g_requestedAtGameplay = false;                       // guest: the snapshot request for this stretch of gameplay went out

bool sendList(NetClient& net, uint8_t slot, const Locker& locker, uint32_t replyTo) {
    std::vector<cargo_transfer::CargoAdd> pieces;
    for (const game::Cargo& piece : locker.pieces) pieces.push_back(cargo_transfer::toAdd(piece));
    const locker_wire::LockerHeader header{locker.key, locker.kind, 0, 0, replyTo, 0};
    return net.send(locker_wire::kMsgLockerList, true, slot, locker_wire::encodeList(header, pieces));
}

bool sendSnapshotEnd(NetClient& net, uint8_t slot) {
    const locker_wire::LockerHeader end{0, 0, locker_wire::kFlagSnapshotEnd, 0, 0, 0};
    return net.send(locker_wire::kMsgLockerList, true, slot, locker_wire::encodeList(end, {}));
}

void sendResult(NetClient& net, uint8_t slot, uint32_t requestId, locker_wire::Reason reason) {
    if (!net.send(locker_wire::kMsgLockerResult, true, slot, proto::bytesOf(locker_wire::resultOf(requestId, reason)))) {
        logger::write("locker_sync: could not send LOCKER_RESULT");
    }
}

// Host: every non-empty locker, then the end marker. False until the first read has found the lockers.
bool sendSnapshot(NetClient& net, uint8_t slot) {
    const auto all = lockers::snapshot();
    if (!all) return false;
    for (const Locker& locker : *all) sendList(net, slot, locker, 0);
    sendSnapshotEnd(net, slot);
    logger::write("locker_sync: sent %zu lockers to slot %u", all->size(), slot);
    return true;
}

void sendOwedSnapshots(NetClient& net, const SessionSnapshot& session) {
    for (const uint8_t slot : g_joins.takeNew(session)) g_owed.insert(slot);
    for (const uint8_t slot : resync::takeRequests(resync::kLockers)) g_owed.insert(slot);
    std::erase_if(g_owed, [&](uint8_t slot) { return sendSnapshot(net, slot); });
}

void sendChanged(NetClient& net) {
    for (const Locker& locker : lockers::takeChanged()) {
        if (sendList(net, proto::kSlotAll, locker, 0)) {
            logger::write("locker_sync: locker %08x kind %u now %zu pieces", locker.key, locker.kind, locker.pieces.size());
        }
    }
}

// Each answer is the locker as it stands (when this world has it), then the result.
void sendReplies(NetClient& net) {
    for (const lockers::Reply& reply : lockers::takeReplies()) {
        if (reply.locker) sendList(net, reply.slot, *reply.locker, reply.requestId);
        sendResult(net, reply.slot, reply.requestId, reply.reason);
    }
}

// Host: a guest's move. A payload that does not parse is answered when it still names a request, so the guest can undo it.
void onMove(NetClient& net, const GameFrame& frame) {
    locker_wire::LockerMove move;
    if (!locker_wire::decodeMove(frame.payload, move)) {
        reject_counters::count(frame.type, Reason::Malformed);
        logger::write("locker_sync: dropped a malformed LOCKER_MOVE (%zu bytes)", frame.payload.size());
        uint32_t requestId = 0;
        if (frame.payload.size() >= sizeof(requestId)) std::memcpy(&requestId, frame.payload.data(), sizeof(requestId));
        if (requestId != 0) sendResult(net, frame.slot, requestId, locker_wire::Reason::Malformed);
        return;
    }
    if (!g_limiters[frame.slot].allow(nowUs())) {
        reject_counters::count(frame.type, Reason::RateLimited);
        sendResult(net, frame.slot, move.requestId, locker_wire::Reason::Rate);
        return;
    }
    lockers::queueMove(frame.slot, move);
}

// Guest: the host's list for one locker, or the end of its snapshot.
void onList(const GameFrame& frame) {
    locker_wire::LockerHeader header;
    std::vector<cargo_transfer::CargoAdd> pieces;
    if (!locker_wire::decodeList(frame.payload, header, pieces)) {
        reject_counters::count(frame.type, Reason::Malformed);
        logger::write("locker_sync: dropped a malformed LOCKER_LIST (%zu bytes)", frame.payload.size());
        return;
    }
    if (header.flags & locker_wire::kFlagSnapshotEnd) return lockers::endSnapshot();
    Locker locker{header.key, header.slotKind, {}};
    for (const cargo_transfer::CargoAdd& piece : pieces) locker.pieces.push_back(cargo_transfer::fromAdd(piece));
    lockers::follow(std::move(locker));
}

void onResult(const GameFrame& frame) {
    locker_wire::LockerResult result;
    if (locker_wire::decodeResult(frame.payload, result)) {
        lockers::onResult(result);
    } else {
        reject_counters::count(frame.type, Reason::Malformed);
        logger::write("locker_sync: dropped a malformed LOCKER_RESULT (%zu bytes)", frame.payload.size());
    }
}

// Guest: once its own gameplay has settled (a list that arrived while the world loaded may have been overwritten by the
// load), ask the host for every locker again.
void requestSnapshotAtGameplay(NetClient& net) {
    if (!game::gameplaySettled()) {
        g_requestedAtGameplay = false;
    } else if (!g_requestedAtGameplay && resync::request(net, g_hostSlot, resync::kLockers)) {
        g_requestedAtGameplay = true;
        logger::write("locker_sync: gameplay started, asked the host for the lockers");
    }
}

void sendLocalMoves(NetClient& net) {
    for (const locker_wire::LockerMove& move : lockers::takeLocalMoves()) {
        if (!net.send(locker_wire::kMsgLockerMove, true, g_hostSlot, proto::bytesOf(move))) {
            logger::write("locker_sync: could not send LOCKER_MOVE %u", move.requestId);
        }
    }
}

}  // namespace

namespace locker_sync {

void onFrame(NetClient& net, const GameFrame& frame) {
    if (frame.type == locker_wire::kMsgLockerMove) {
        if (!g_guest && frame.slot != g_hostSlot) onMove(net, frame);
    } else if (g_guest && frame.slot == g_hostSlot) {
        if (frame.type == locker_wire::kMsgLockerList) onList(frame);
        if (frame.type == locker_wire::kMsgLockerResult) onResult(frame);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    lockers::setRole(host, g_guest);
    if (host) {
        sendOwedSnapshots(net, session);
        sendChanged(net);
        sendReplies(net);
    } else {
        g_joins.clear();
        g_owed.clear();
        g_limiters.clear();
    }
    if (g_guest) {
        sendLocalMoves(net);
        requestSnapshotAtGameplay(net);
    } else {
        g_requestedAtGameplay = false;
    }
}

}  // namespace locker_sync
