#include "authority_sync.h"

#include <mutex>
#include <set>
#include <vector>

#include "log.h"
#include "reject_counters.h"
#include "resync.h"

namespace {

using authority_wire::AuthMessage;

struct Outgoing {
    uint16_t type;
    AuthMessage message;
};

std::mutex g_mutex;  // guards the table and the outbox (any thread acts, the net thread applies and sends)
authority::AuthorityTable g_table;
std::vector<Outgoing> g_outbox;

// Net thread only.
std::set<uint8_t> g_peers;
uint32_t g_epoch = 0;
bool g_linked = false;

// Queues the message a local action produced, if the rules allowed one. Caller holds the lock.
bool queue(uint16_t type, const std::optional<AuthMessage>& message) {
    if (message) g_outbox.push_back({type, *message});
    return message.has_value();
}

// Net thread. Repeats the local claims so a peer that missed them (it joined, or asked) learns them.
void announceLocalClaims(uint8_t localSlot) {
    std::lock_guard lock(g_mutex);
    for (const AuthMessage& claim : g_table.claimsOf(localSlot)) g_outbox.push_back({authority_wire::kMsgAuthClaim, claim});
}

void followSession(const SessionSnapshot& session) {
    std::set<uint8_t> peers;
    for (const PeerInfo& peer : session.peers) peers.insert(peer.slot);
    const bool newSession = !session.linked || !g_linked || session.epoch != g_epoch;
    bool joined = false;
    {
        std::lock_guard lock(g_mutex);
        if (newSession) {
            g_table.clear();
            g_outbox.clear();
            g_peers.clear();
        }
        g_table.setSession(session.linked ? session.localSlot : authority_wire::kNoOwner,
                           session.linked ? session.hostSlot : authority_wire::kNoOwner);
        for (const uint8_t slot : g_peers) {
            if (!peers.contains(slot)) g_table.dropSlot(slot);
        }
        for (const uint8_t slot : peers) joined = joined || !g_peers.contains(slot);
    }
    g_peers = peers;
    g_linked = session.linked;
    g_epoch = session.epoch;
    if (session.linked && joined) announceLocalClaims(session.localSlot);
}

}  // namespace

namespace authority_sync {

bool claim(uint64_t id) {
    std::lock_guard lock(g_mutex);
    return queue(authority_wire::kMsgAuthClaim, g_table.claim(id));
}

bool assign(uint64_t id, uint8_t owner) {
    std::lock_guard lock(g_mutex);
    return queue(authority_wire::kMsgAuthClaim, g_table.assign(id, owner));
}

bool release(uint64_t id) {
    std::lock_guard lock(g_mutex);
    return queue(authority_wire::kMsgAuthStop, g_table.release(id));
}

bool stop(uint64_t id) {
    std::lock_guard lock(g_mutex);
    return queue(authority_wire::kMsgAuthStop, g_table.stop(id));
}

bool decline(uint64_t id) {
    std::lock_guard lock(g_mutex);
    return queue(authority_wire::kMsgAuthDecline, g_table.decline(id));
}

bool isOwner(uint64_t id) {
    std::lock_guard lock(g_mutex);
    return g_table.isOwner(id);
}

uint8_t ownerOf(uint64_t id) {
    std::lock_guard lock(g_mutex);
    return g_table.ownerOf(id);
}

uint8_t pickOwner(uint64_t id, std::span<const uint8_t> candidates) {
    std::lock_guard lock(g_mutex);
    return g_table.pickOwner(id, candidates);
}

void setChangeHandler(authority::AuthorityTable::ChangeHandler handler) {
    std::lock_guard lock(g_mutex);
    g_table.setChangeHandler(std::move(handler));
}

void onFrame(const GameFrame& frame) {
    if (!authority_wire::isAuthType(frame.type)) return;
    AuthMessage message;
    if (!authority_wire::decode(frame.payload, message)) {
        reject_counters::count(frame.type, reject_counters::Reason::Malformed);
        return;
    }
    std::lock_guard lock(g_mutex);
    const authority::Outcome outcome = g_table.onMessage(frame.type, frame.slot, message);
    if (const auto reason = authority::rejectReason(outcome)) reject_counters::count(frame.type, *reason);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    followSession(session);
    if (!session.linked) return;
    if (!resync::takeRequests(resync::kAuthority).empty()) announceLocalClaims(session.localSlot);
    std::vector<Outgoing> outgoing;
    {
        std::lock_guard lock(g_mutex);
        outgoing.swap(g_outbox);
    }
    for (const Outgoing& out : outgoing) {
        if (!net.send(out.type, true, proto::kSlotAll, proto::bytesOf(out.message))) {
            logger::write("authority_sync: could not send 0x%04X for object %llx", out.type,
                          static_cast<unsigned long long>(out.message.id));
        }
    }
}

}  // namespace authority_sync
