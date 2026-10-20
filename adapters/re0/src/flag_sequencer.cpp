#include "flag_sequencer.h"

#include <mutex>

#include "character_owner.h"
#include "debug_stats.h"
#include "flag_sequencer_rule.h"
#include "frame_router.h"
#include "log.h"
#include "resync.h"
#include "state_net.h"

namespace {

namespace rule = flag_sequencer_rule;
namespace wire = flag_sequencer_wire;
using flag_diff::Change;
using flag_diff::Words;

struct Proposal {
    uint8_t origin;
    std::vector<Change> changes;
};

std::mutex g_mutex;
std::vector<Proposal> g_proposals;  // guarded by g_mutex: net thread to game thread, host only
std::vector<wire::Commit> g_commits;  // guarded by g_mutex: net thread to game thread, guests only

rule::HostOrder g_hostOrder;    // game thread
rule::GuestOrder g_guestOrder;  // game thread

void onPropose(const GameFrame& frame) {
    auto changes = wire::decodePropose(frame.payload);
    if (!changes) {
        logger::writeUnlessRepeated("flag_seq: refused a malformed proposal from slot %d", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    g_proposals.push_back({frame.slot, std::move(*changes)});
}

void onCommit(const GameFrame& frame) {
    auto commit = wire::decodeCommit(frame.payload);
    if (!commit) {
        logger::writeUnlessRepeated("flag_seq: refused a malformed commit from slot %d", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    g_commits.push_back(std::move(*commit));
}

void logCommit(const wire::Commit& commit) {
    uint32_t set = 0;
    uint32_t cleared = 0;
    for (const Change& change : commit.changes) {
        set += rule::bitCount(change.set);
        cleared += rule::bitCount(change.clear);
    }
    logger::write("flag_seq: commit %u (%u set, %u cleared) from slot %d", commit.serial, set, cleared, commit.origin);
    debug_stats::count(debug_stats::Counter::FlagCommits);
}

// Host: numbers and broadcasts `changes` (empty = the answer to a proposal that changed nothing).
void commit(uint8_t origin, std::vector<Change> changes) {
    const wire::Commit issued = g_hostOrder.issue(origin, std::move(changes));
    if (!issued.changes.empty()) logCommit(issued);
    state_net::send(wire::kMsgFlagCommit, true, wire::encodeCommit(issued));
}

void logOverrides(const std::vector<Change>& overrode) {
    for (const Change& change : overrode) {
        for (uint32_t bit = 0; bit < 32; ++bit) {
            const uint32_t mask = 1u << bit;
            if (!((change.set | change.clear) & mask)) continue;
            logger::write("flag_seq: corrected bit 0x%x to %u", change.word * 32u + bit, (change.set & mask) ? 1u : 0u);
            debug_stats::count(debug_stats::Counter::FlagCorrections);
        }
    }
}

std::vector<Change> collectAsHost(const Words& words) {
    std::vector<Proposal> proposals;
    {
        std::lock_guard lock(g_mutex);
        proposals.swap(g_proposals);
    }
    Words model = words;
    std::vector<Change> ordered;
    for (Proposal& proposal : proposals) {
        std::vector<Change> changes = rule::effective(model, proposal.changes);
        for (const Change& change : changes) flag_diff::apply(model, change);
        ordered.insert(ordered.end(), changes.begin(), changes.end());
        commit(proposal.origin, std::move(changes));
    }
    return ordered;
}

std::vector<Change> collectAsGuest(const Words& words) {
    std::vector<wire::Commit> commits;
    {
        std::lock_guard lock(g_mutex);
        commits.swap(g_commits);
    }
    Words model = words;
    std::vector<Change> ordered;
    for (const wire::Commit& incoming : commits) {
        const rule::Receipt receipt = g_guestOrder.receive(incoming, character_owner::localSlot(), model);
        if (!receipt.apply) continue;
        if (receipt.gap) {
            logger::write("flag_seq: gap %u -> %u, resync", receipt.expected, incoming.serial);
            resync::request("flag commits skipped");
        }
        if (!incoming.changes.empty()) logCommit(incoming);
        logOverrides(receipt.overrode);
        ordered.insert(ordered.end(), incoming.changes.begin(), incoming.changes.end());
    }
    return ordered;
}

}  // namespace

namespace flag_sequencer {

bool enable() {
    return frame_router::add(wire::kMsgFlagPropose, frame_router::SenderPolicy::GuestOnly, onPropose) &&
           frame_router::add(wire::kMsgFlagCommit, frame_router::SenderPolicy::HostOnly, onCommit);
}

void submitLocal(const std::vector<Change>& changes) {
    if (character_owner::isHost()) {
        commit(character_owner::localSlot(), changes);
        return;
    }
    if (state_net::send(wire::kMsgFlagPropose, true, wire::encodePropose(changes))) {
        g_guestOrder.proposed(changes);
        debug_stats::count(debug_stats::Counter::FlagWordsSent, static_cast<uint32_t>(changes.size()));
    }
}

std::vector<Change> collect(const Words& words) {
    return character_owner::isHost() ? collectAsHost(words) : collectAsGuest(words);
}

uint32_t issuedSerial() { return g_hostOrder.serial(); }

void adoptSerial(uint32_t serial) { g_guestOrder.adopt(serial); }

void reset() { g_guestOrder.reset(); }

}  // namespace flag_sequencer
