#include "fact_sync.h"

#include <algorithm>
#include <deque>
#include <vector>

#include "fact_wire.h"
#include "game.h"
#include "log.h"

namespace {

constexpr size_t kMaxPending = 4096;  // guest: facts received before the fact database was found

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
std::deque<fact_wire::Entry> g_pending;

void send(NetClient& net, const std::vector<fact_wire::Entry>& facts) {
    for (size_t first = 0; first < facts.size(); first += fact_wire::kMaxEntries) {
        const size_t count = std::min<size_t>(fact_wire::kMaxEntries, facts.size() - first);
        if (!net.send(fact_wire::kMsgFactSet, true, proto::kSlotAll, fact_wire::encode({facts.data() + first, count}))) {
            logger::write("fact_sync: could not send %zu facts", count);
        }
    }
    logger::write("fact_sync: sent %zu facts", facts.size());
}

void applyPending() {
    size_t applied = 0;
    while (!g_pending.empty() && game::applyFact(g_pending.front())) {
        g_pending.pop_front();
        ++applied;
    }
    if (applied) logger::write("fact_sync: applied %zu facts, %zu waiting", applied, g_pending.size());
}

}  // namespace

namespace fact_sync {

void onFrame(const GameFrame& frame) {
    std::vector<fact_wire::Entry> facts;
    if (frame.type != fact_wire::kMsgFactSet || !g_guest || frame.slot != g_hostSlot) return;
    if (!fact_wire::decode(frame.payload, facts)) {
        logger::write("fact_sync: dropped a malformed FACT_SET (%zu bytes)", frame.payload.size());
        return;
    }
    for (const fact_wire::Entry& fact : facts) {
        if (g_pending.size() < kMaxPending) g_pending.push_back(fact);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    game::shareFactWrites(host);
    if (host) {
        const std::vector<fact_wire::Entry> facts = game::takeFactWrites();
        if (!facts.empty()) send(net, facts);
    } else if (g_guest) {
        applyPending();
    } else {
        g_pending.clear();
    }
}

}  // namespace fact_sync
