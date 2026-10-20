#include "join_extras.h"

#include <atomic>
#include <mutex>

#include "flag_sequencer.h"
#include "frame_router.h"
#include "item_ledger.h"
#include "log.h"
#include "prop_sync.h"
#include "room_authority.h"
#include "session_wire.h"
#include "state_net.h"

namespace {

using session_wire::JoinExtras;

static_assert(session_wire::kMaxProps == prop_rule::kMaxJournal, "JOIN_EXTRAS carries the whole prop journal");

struct Arrived {
    JoinExtras extras;
    uint8_t sender;
};

std::mutex g_mutex;
std::optional<Arrived> g_arrived;  // guarded by g_mutex: net thread to game thread, guest only
std::atomic<uint32_t> g_serial{0};  // host: the newest serial sent
std::atomic<bool> g_snapshotApplied{false};  // guest: the snapshot the extras belong to is applied

prop_wire::Commit wireOf(const prop_rule::Record& record) {
    prop_wire::Commit commit{};
    commit.serial = record.serial;
    commit.scene = record.key.scene;
    commit.kind = prop_wire::kObstacleKind;
    commit.unitId = record.key.unit;
    for (size_t axis = 0; axis < record.pose.pos.size(); ++axis) commit.pos[axis] = record.pose.pos[axis];
    commit.yaw = record.pose.yaw;
    commit.committer = record.committer;
    return commit;
}

prop_rule::Record recordOf(const prop_wire::Commit& commit) {
    return {{commit.scene, commit.unitId},
            commit.serial,
            commit.committer,
            {{commit.pos[0], commit.pos[1], commit.pos[2]}, commit.yaw}};
}

void onExtras(const GameFrame& frame) {
    auto extras = session_wire::decode(frame.payload);
    if (!extras) {
        logger::writeUnlessRepeated("join_extras: refused a malformed message from slot %d", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    g_arrived = Arrived{std::move(*extras), frame.slot};
}

}  // namespace

namespace join_extras {

bool enable() { return frame_router::add(session_wire::kMsgJoinExtras, frame_router::SenderPolicy::HostOnly, onExtras); }

void send() {
    JoinExtras extras;
    extras.snapshotSerial = g_serial.load() + 1;
    extras.flagSerial = flag_sequencer::issuedSerial();
    extras.claims = room_authority::claims();
    for (const prop_rule::Record& record : prop_sync::commits()) extras.props.push_back(wireOf(record));
    for (const item_ledger::Key& key : item_ledger::consumed()) extras.items.push_back(session_wire::takeOf(key));
    if (!state_net::send(session_wire::kMsgJoinExtras, true, session_wire::encode(extras))) return;
    g_serial = extras.snapshotSerial;
    logger::write("join_extras: sent serial %u (claims %zu, prop commits %zu, items taken %zu, flag commit %u)",
                  extras.snapshotSerial, extras.claims.size(), extras.props.size(), extras.items.size(), extras.flagSerial);
}

uint32_t lastSerial() { return g_serial.load(); }

void onSnapshotApplied() { g_snapshotApplied = true; }

void reset() {
    std::lock_guard lock(g_mutex);
    g_arrived.reset();
    g_snapshotApplied = false;
}

std::optional<uint32_t> applyArrived() {
    if (!g_snapshotApplied.load()) return std::nullopt;
    std::optional<Arrived> arrived;
    {
        std::lock_guard lock(g_mutex);
        arrived.swap(g_arrived);
    }
    if (!arrived) return std::nullopt;
    const JoinExtras& extras = arrived->extras;
    room_authority::applyClaims(arrived->sender, extras.claims);
    std::vector<prop_rule::Record> records;
    for (const prop_wire::Commit& commit : extras.props) records.push_back(recordOf(commit));
    prop_sync::adoptCommits(records);
    std::vector<item_ledger::Key> taken;
    for (const session_wire::ItemTake& take : extras.items) taken.push_back(session_wire::keyOf(take));
    item_ledger::adoptConsumed(taken);
    flag_sequencer::adoptSerial(extras.flagSerial);
    logger::write("join_extras: applied serial %u (claims %zu, prop commits %zu, items taken %zu, flag commit %u)",
                  extras.snapshotSerial, extras.claims.size(), extras.props.size(), extras.items.size(), extras.flagSerial);
    return extras.snapshotSerial;
}

}  // namespace join_extras
