#include "story.h"

#include <windows.h>

#include <optional>
#include <set>
#include <unordered_map>

#include "link.h"
#include "log.h"
#include "presence_rule.h"
#include "stage_guard.h"
#include "stage_packets.h"
#include "story_rule.h"
#include "troc_catalogue.h"
#include "wire_cd.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kSummaryMs = 30000;
constexpr uint32_t kAppliedState = 1;  // the state the design expects in an applied Ack [I]

struct Opcodes {
    uint16_t req = 0, ack = 0, prepare = 0, del = 0;
} g_op;

// One of our own stage instances, as our own traffic shows it.
struct Instance {
    std::optional<uint32_t> key;                      // its sequencer key (PrepareSequencerAck)
    std::optional<uint32_t> sitting;                  // the branch of its last applied Ack
    std::optional<stage_packets::BranchReq> pending;  // a Req (the game's or our replay) not answered yet
    std::optional<uint32_t> replaySeq;                // the host entry our pending Req replays
};

bool g_replay = false;
std::unordered_map<uint64_t, Instance> g_instances;
std::unordered_map<uint32_t, uint64_t> g_instanceOfKey;
std::optional<story_rule::HostLog> g_host;  // made at the host's first entry
story_rule::GuestLog g_guest;
std::set<uint32_t> g_reported;  // guest: entries already logged
uint16_t g_loadSerial = 0;

// The live check of the Ack's fields (static_s14_s16.md 1).
struct Seen {
    uint32_t reqs = 0, applyAcks = 0, branchAt13 = 0, branchAt17 = 0, stateApplied = 0, prepares = 0, deletes = 0;
} g_seen;
uint64_t g_lastSummaryMs = 0, g_lastActivity = 0;
story::Stats g_stats;

uint64_t activity() {
    return uint64_t{g_seen.reqs} + g_seen.applyAcks + g_seen.prepares + g_seen.deletes + g_stats.received + g_stats.posted;
}

uint32_t newLogId() {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    return static_cast<uint32_t>(qpc.QuadPart ^ (qpc.QuadPart >> 32)) | 1;  // never 0
}

void send(uint32_t logId, const story_rule::Entry& entry) {
    const wire_cd::StageBranchMessage message{logId, entry.seq, entry.sequencerKey, entry.fromBranch, entry.branch, entry.flags};
    link::send(wire_cd::kStageBranch, true, wire_cd::encode(message));
}

// Host: our own server applied a branch our game asked for.
void logBranch(const Instance& instance, const stage_packets::BranchReq& req) {
    if (!link::isHost() || !instance.key) return;
    if (!g_host) g_host.emplace(newLogId());
    const auto entry = g_host->append(*instance.key, instance.sitting.value_or(story_rule::kUnknownBranch), req.branch, req.flags);
    if (!entry) return;
    ++g_stats.logged;
    send(g_host->logId(), *entry);
}

void onReq(std::span<const uint8_t> payload) {
    const auto req = stage_packets::parseReq(payload);
    if (!req) return;
    ++g_seen.reqs;
    Instance& instance = g_instances[req->instance];
    instance.pending = *req;
    instance.replaySeq.reset();  // the game's own choice
}

void onAck(std::span<const uint8_t> payload) {
    const auto ack = stage_packets::parseAck(payload);
    if (!ack || ack->mode != stage_packets::kModeApply) return;
    ++g_seen.applyAcks;
    if (ack->state == kAppliedState) ++g_seen.stateApplied;
    Instance& instance = g_instances[ack->instance];
    if (instance.pending) {
        const bool answered = ack->branch == instance.pending->branch;
        if (answered) ++g_seen.branchAt13;
        if (ack->state == instance.pending->branch) ++g_seen.branchAt17;
        if (answered && instance.replaySeq && instance.key) {
            g_guest.applied(*instance.key, *instance.replaySeq);
            ++g_stats.applied;
        } else if (answered) {
            logBranch(instance, *instance.pending);
        }
        instance.pending.reset();
        instance.replaySeq.reset();
    }
    instance.sitting = ack->branch;
}

void onPrepare(std::span<const uint8_t> payload) {
    const auto prepare = stage_packets::parsePrepare(payload);
    if (!prepare) return;
    ++g_seen.prepares;
    g_instances[prepare->instance].key = prepare->sequencerKey;
    g_instanceOfKey[prepare->sequencerKey] = prepare->instance;
}

void onDelete(std::span<const uint8_t> payload) {
    const auto instance = stage_packets::parseDelete(payload);
    if (!instance) return;
    ++g_seen.deletes;
    const auto it = g_instances.find(*instance);
    if (it == g_instances.end()) return;
    if (it->second.key) g_instanceOfKey.erase(*it->second.key);
    g_instances.erase(it);
}

// A load takes the field's stage manager and instances with it; the queues (static keys) stay.
void followLoads() {
    const uint16_t serial = world_gate::report().loadSerial;
    if (serial == g_loadSerial) return;
    g_loadSerial = serial;
    stage_guard::reset();
    g_instances.clear();
    g_instanceOfKey.clear();
}

// Guest: each waiting entry whose stage our world has. The guard's table decides the range; with replay off (or the
// table unreadable) nothing is posted and each entry is logged once with what the guard read.
void runQueue(uint64_t nowMs) {
    for (uint32_t key : g_guest.waitingKeys()) {
        const auto found = g_instanceOfKey.find(key);
        if (found == g_instanceOfKey.end()) continue;  // our world has not reached the stage: the entry waits
        Instance& instance = g_instances[found->second];
        const auto table = stage_guard::table(found->second);
        // Observing, every branch counts as in range so the log shows what the replay would meet.
        const uint32_t count = g_replay ? (table ? table->count : 0) : UINT32_MAX;
        const story_rule::Local local{true, instance.pending.has_value(), instance.sitting, count};
        const auto entry = g_guest.next(key, local, nowMs);
        if (!entry) continue;
        const bool safe = table && stage_guard::branchOk(*table, entry->branch);
        if (g_reported.insert(entry->seq).second) {
            ++g_stats.wouldApply;
            logger::write("story: %s key 0x%X branch %u (host left %d) on instance 0x%llX, branch table %s %u, entry %s",
                          g_replay && safe ? "replaying" : "would replay", key, entry->branch, static_cast<int>(entry->fromBranch),
                          static_cast<unsigned long long>(found->second), table ? "count" : "unreadable", table ? table->count : 0,
                          safe ? "ok" : "refused");
        }
        if (!g_replay || !safe) continue;
        const stage_packets::BranchReq req{found->second, entry->branch, entry->flags};
        funnels::post(presence_rule::packetOf(g_op.req, stage_packets::encodeReq(req)));
        instance.pending = req;
        instance.replaySeq = entry->seq;
        ++g_stats.posted;
    }
}

void summarize(uint64_t nowMs) {
    if (nowMs - g_lastSummaryMs < kSummaryMs || activity() == g_lastActivity) return;
    g_lastSummaryMs = nowMs;
    g_lastActivity = activity();
    logger::write("story: %u reqs, %u apply acks (requested branch at +13 %u, at +17 %u; state 1 %u), %u prepares, %u deletes, "
                  "%zu keys; host log %u, guest queue %zu, replays %u posted %u applied",
                  g_seen.reqs, g_seen.applyAcks, g_seen.branchAt13, g_seen.branchAt17, g_seen.stateApplied, g_seen.prepares,
                  g_seen.deletes, g_instanceOfKey.size(), g_stats.logged, g_guest.pending(), g_stats.posted, g_stats.applied);
}

}  // namespace

namespace story {

bool resolveOpcodes() {
    const troc_table::Table& table = troc_catalogue::table();
    const auto req = table.opcodeOf("StageBranchStateReq"), ack = table.opcodeOf("StageBranchStateAck");
    const auto prepare = table.opcodeOf("PrepareSequencerAck"), del = table.opcodeOf("DeleteSequencerAck");
    if (!req || !ack || !prepare || !del) return false;
    g_op = {*req, *ack, *prepare, *del};
    for (uint16_t opcode : {g_op.req, g_op.ack, g_op.prepare, g_op.del}) funnels::watch(opcode, true);
    return true;
}

void setReplay(bool on) {
    g_replay = on;
    logger::write("story: replay %s", on ? "on (guarded)" : "off (observe only)");
}

void onEvent(const funnels::Event& event, uint64_t) {
    if (!g_op.req) return;
    followLoads();  // world_gate saw this event first: a load it starts clears before the load's own prepares
    const uint16_t opcode = event.header.opcode;
    const std::span<const uint8_t> payload(event.payload);
    if (event.direction == packet::Direction::ClientToServer) {
        if (opcode == g_op.req) onReq(payload);
        return;
    }
    if (opcode == g_op.ack) onAck(payload);
    if (opcode == g_op.prepare) onPrepare(payload);
    if (opcode == g_op.del) onDelete(payload);
}

void tick(uint64_t nowMs, bool peerJoined) {
    if (!g_op.req) return;
    followLoads();
    if (peerJoined && g_host && link::isHost()) {
        for (const story_rule::Entry& entry : g_host->entries()) send(g_host->logId(), entry);
    }
    if (!link::isHost()) runQueue(nowMs);
    summarize(nowMs);
}

bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload) {
    if (type != wire_cd::kStageBranch) return false;
    const auto message = wire_cd::decodeStageBranch(payload);
    if (!message || link::hostSlot() != slot) return false;
    g_guest.begin(message->logId);
    if (g_guest.offer({message->seq, message->sequencerKey, message->fromBranch, message->branch, message->flags})) ++g_stats.received;
    return true;
}

bool replayWaiting() {
    for (const auto& [id, instance] : g_instances) {
        if (instance.replaySeq) return true;
    }
    return false;
}

void abandonReplays() {
    for (auto& [id, instance] : g_instances) {
        if (!instance.replaySeq) continue;
        instance.pending.reset();
        instance.replaySeq.reset();
    }
}

Stats stats() { return g_stats; }

}  // namespace story
