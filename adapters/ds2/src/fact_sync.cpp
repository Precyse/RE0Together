#include "fact_sync.h"

#include <algorithm>
#include <deque>
#include <set>
#include <vector>

#include "fact_snapshot.h"
#include "fact_wire.h"
#include "game.h"
#include "log.h"
#include "reject_counters.h"
#include "resync.h"

namespace {

constexpr size_t kMaxPending = 32768;  // guest: facts received before the fact database was found (a snapshot and deltas)

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
std::deque<fact_wire::Entry> g_pending;
fact_snapshot::Progress g_progress;
bool g_requestedAtGameplay = false;  // guest: the snapshot request for this stretch of gameplay went out
std::set<uint8_t> g_snapshotted;     // host: peers that have been sent a snapshot since they joined
uint32_t g_snapshotId = 0;

void send(NetClient& net, const std::vector<fact_wire::Entry>& facts) {
    for (size_t first = 0; first < facts.size(); first += fact_wire::kMaxEntries) {
        const size_t count = std::min<size_t>(fact_wire::kMaxEntries, facts.size() - first);
        if (!net.send(fact_wire::kMsgFactSet, true, proto::kSlotAll, fact_wire::encode({facts.data() + first, count}))) {
            logger::write("fact_sync: could not send %zu facts", count);
        }
    }
    logger::write("fact_sync: sent %zu facts", facts.size());
}

// Host: every fact it has changed since sharing started, to `destSlot` (proto::kSlotAll for everyone).
void sendSnapshot(NetClient& net, uint8_t destSlot) {
    const std::vector<fact_wire::Entry> facts = game::factSnapshot();
    const std::vector<std::vector<uint8_t>> chunks = fact_snapshot::encode(++g_snapshotId, facts);
    size_t sent = 0;
    for (const std::vector<uint8_t>& chunk : chunks) sent += net.send(fact_snapshot::kMsgFactSnapshot, true, destSlot, chunk);
    logger::write("fact_sync: snapshot %u of %zu facts in %zu chunks to slot %u (%zu sent)", g_snapshotId, facts.size(),
                  chunks.size(), destSlot, sent);
}

void queuePending(const std::vector<fact_wire::Entry>& facts, uint16_t messageType) {
    for (const fact_wire::Entry& fact : facts) {
        if (g_pending.size() < kMaxPending) {
            g_pending.push_back(fact);
        } else {
            reject_counters::count(messageType, reject_counters::Reason::Overflow);
        }
    }
}

void applyPending() {
    size_t applied = 0;
    while (!g_pending.empty() && game::applyFact(g_pending.front())) {
        g_pending.pop_front();
        ++applied;
    }
    if (applied) logger::write("fact_sync: applied %zu facts, %zu waiting", applied, g_pending.size());
}

// Host: a snapshot for every peer that just joined and for every peer that asked for one.
void answerSnapshots(NetClient& net, const SessionSnapshot& session) {
    std::set<uint8_t> present;
    for (const PeerInfo& peer : session.peers) present.insert(peer.slot);
    std::erase_if(g_snapshotted, [&](uint8_t slot) { return !present.contains(slot); });
    for (const uint8_t slot : present) {
        if (g_snapshotted.insert(slot).second) sendSnapshot(net, slot);
    }
    for (const uint8_t slot : resync::takeRequests(resync::kFacts)) sendSnapshot(net, slot);
}

// Guest: once its own gameplay has started (a snapshot that arrived while the world loaded may have been overwritten by
// the load), ask the host for the whole picture again.
void requestSnapshotAtGameplay(NetClient& net) {
    const bool settled = game::gameplaySettled();
    if (!settled) {
        g_requestedAtGameplay = false;
    } else if (!g_requestedAtGameplay && resync::request(net, g_hostSlot, resync::kFacts)) {
        g_requestedAtGameplay = true;
        logger::write("fact_sync: gameplay started, asked the host for a fact snapshot");
    }
}

}  // namespace

namespace fact_sync {

void onFrame(const GameFrame& frame) {
    const bool isDelta = frame.type == fact_wire::kMsgFactSet;
    const bool isSnapshot = frame.type == fact_snapshot::kMsgFactSnapshot;
    if ((!isDelta && !isSnapshot) || !g_guest || frame.slot != g_hostSlot) return;
    std::vector<fact_wire::Entry> facts;
    if (isDelta) {
        if (!fact_wire::decode(frame.payload, facts)) {
            reject_counters::count(frame.type, reject_counters::Reason::Malformed);
            logger::write("fact_sync: dropped a malformed FACT_SET (%zu bytes)", frame.payload.size());
            return;
        }
    } else {
        fact_snapshot::ChunkHeader header;
        if (!fact_snapshot::decode(frame.payload, header, facts)) {
            reject_counters::count(frame.type, reject_counters::Reason::Malformed);
            logger::write("fact_sync: dropped a malformed FACT_SNAPSHOT (%zu bytes)", frame.payload.size());
            return;
        }
        if (g_progress.onChunk(header) == fact_snapshot::Progress::State::Complete) {
            logger::write("fact_sync: snapshot %u complete (%u chunks)", header.snapshotId, header.count);
        }
    }
    queuePending(facts, frame.type);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    game::shareFactWrites(host);
    if (host) {
        const std::vector<fact_wire::Entry> facts = game::takeFactWrites();
        if (!facts.empty()) send(net, facts);
        answerSnapshots(net, session);
    } else {
        g_snapshotted.clear();
    }
    if (g_guest) {
        applyPending();
        requestSnapshotAtGameplay(net);
    } else {
        g_pending.clear();
        g_requestedAtGameplay = false;
    }
}

}  // namespace fact_sync
