#include "boss_handover.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

#include "boss_handover_rule.h"
#include "boss_wire.h"
#include "character_owner.h"
#include "debug_stats.h"
#include "door_travel.h"
#include "enemy_lease.h"
#include "enemy_observer.h"
#include "enemy_observer_wire.h"
#include "frame_router.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "resync.h"
#include "room_authority.h"
#include "scene.h"
#include "split_rooms.h"
#include "state_net.h"

namespace {

namespace rule = boss_handover_rule;
namespace wire = boss_wire;
using debug_stats::Counter;
using enemy_observer_wire::PoseEntry;
using Members = std::vector<wire::Member>;

constexpr size_t kMaxInbox = 16;
constexpr uint32_t kWatchIntervalMs = 250;  // how often the members of a room another machine runs are noted

struct Arrival {
    uint8_t sender;
    wire::Message message;
};

std::mutex g_mutex;
std::vector<Arrival> g_inbox;  // guarded by g_mutex: net thread to game thread

// Everything below is the game thread's.

// How the loaded room stands this frame.
struct World {
    uint32_t nowMs = 0;
    uint16_t scene = scene::kNone;
    std::optional<room_authority::Holder> holder;
    bool linked = false;     // a remote peer is present
    bool peerHere = false;   // the peer's report says it stands in the loaded room
    bool peerGone = false;   // the peer's report says it stands elsewhere
    bool together = false;   // linked and not apart (the enemy observers' condition)
};

rule::Machine g_machine;
Members g_members;                          // the snapshot: the old authority's frozen members, or its copy on the new one
uint16_t g_scene = scene::kNone;            // the transaction's room
uint32_t g_epoch = 0;                       // the room authority's epoch for it when the transaction began
uint32_t g_lastToken = 0;
bool g_doorLatched = false;                 // a prepare was tried for the door that is running
Members g_watched;                          // the boss members seen in the room another machine runs
uint32_t g_watchedMs = 0;
uint16_t g_peerHeldScene = scene::kNone;    // the room another machine ran at the last tick

// ---- Net thread --------------------------------------------------------------------------------------------------

void onFrame(const GameFrame& frame) {
    std::optional<wire::Message> message = wire::decode(frame.payload);
    if (!message) {
        logger::writeUnlessRepeated("boss: type 0x%04x from slot %u refused malformed", frame.type, frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_inbox.size() >= kMaxInbox) g_inbox.erase(g_inbox.begin());
    g_inbox.push_back({frame.slot, std::move(*message)});
}

// ---- Game thread: the world and the members ----------------------------------------------------------------------

World readWorld() {
    World world;
    world.nowMs = pose_wire::clockMs();
    world.scene = scene::current();
    world.linked = net_pad::active();
    if (world.scene != scene::kNone) world.holder = room_authority::holderOf(world.scene);
    if (world.linked) {
        const door_travel::PeerPlace place = door_travel::peerPlace();
        world.peerHere = place == door_travel::PeerPlace::Here;
        world.peerGone = place == door_travel::PeerPlace::Elsewhere;
        world.together = !split_rooms::apart();
    }
    return world;
}

rule::Local localOf(const wire::Member& member) {
    const std::optional<PoseEntry> entry = enemy_observer::snapshot(member.slot);
    if (!entry) return {};
    return {true, entry->classIndex, entry->spawnId, entry->hp};
}

std::vector<rule::Local> localsOf(const Members& members) {
    std::vector<rule::Local> locals;
    locals.reserve(members.size());
    for (const wire::Member& member : members) locals.push_back(localOf(member));
    return locals;
}

wire::Member memberOf(const PoseEntry& entry) {
    wire::Member member{};
    member.slot = entry.slot;
    member.classIndex = entry.classIndex;
    member.spawnId = entry.spawnId;
    member.hp = entry.hp;
    for (int i = 0; i < 3; ++i) member.pos[i] = entry.pos[i];
    member.quat = entry.quat;
    for (int i = 0; i < 4; ++i) member.record[i] = entry.record[i];
    return member;
}

PoseEntry entryOf(const wire::Member& member, int16_t hp) {
    PoseEntry entry{};
    entry.slot = member.slot;
    entry.classIndex = member.classIndex;
    entry.spawnId = member.spawnId;
    entry.hp = hp;
    entry.flags = enemy_observer_wire::kFlagAlive;
    for (int i = 0; i < 3; ++i) entry.pos[i] = member.pos[i];
    entry.quat = member.quat;
    for (int i = 0; i < 4; ++i) entry.record[i] = member.record[i];
    return entry;
}

// The living boss members of the loaded room, in pool order.
Members collectMembers() {
    Members members;
    for (int slot = 0; slot < game::kEnemyPoolSlots && members.size() < wire::kMaxMembers; ++slot) {
        const std::optional<PoseEntry> entry = enemy_observer::snapshot(slot);
        if (entry && wire::bossClass(entry->classIndex) && entry->hp > 0) members.push_back(memberOf(*entry));
    }
    return members;
}

bool anyLeased(const Members& members) {
    for (const wire::Member& member : members) {
        if (enemy_lease::busy(member.slot)) return true;
    }
    return false;
}

void holdMembers() {
    for (const wire::Member& member : g_members) enemy_observer::freeze(member.slot);
}

// The members carry on from the snapshot's record at step 0 (the freeze ends).
void restartMembers() {
    for (const wire::Member& member : g_members) enemy_observer::startAt(member.slot, member.record);
}

// ---- Game thread: messages and log lines -------------------------------------------------------------------------

wire::Header headerOf(uint32_t token, uint32_t epoch, uint16_t scene, wire::Phase phase) {
    wire::Header header{};
    header.token = token;
    header.roomEpoch = epoch;
    header.scene = scene;
    header.phase = static_cast<uint8_t>(phase);
    return header;
}

void send(const wire::Header& header, const Members& members = {}) {
    state_net::send(wire::kMsgBossHandover, true, wire::encode({header, members}));
}

void sendCancel(uint32_t token, uint32_t epoch, uint16_t scene, wire::Reason reason) {
    wire::Header header = headerOf(token, epoch, scene, wire::Phase::Cancel);
    header.reason = static_cast<uint8_t>(reason);
    send(header);
}

void logPhase(const char* word) {
    logger::write("boss: token %u %s (members %u, alive %02x)", g_machine.token(), word, g_machine.count(), g_machine.alive());
}

void logCancel(uint32_t token, wire::Reason reason) {
    logger::write("boss: token %u cancelled (%s)", token, wire::name(reason));
}

// ---- Game thread: the old authority ------------------------------------------------------------------------------

// The old authority takes its members back and carries on: nothing was handed over.
void cancelOld(wire::Reason reason, bool tellPeer) {
    if (tellPeer) sendCancel(g_machine.token(), g_epoch, g_scene, reason);
    restartMembers();
    logCancel(g_machine.token(), reason);
    debug_stats::count(Counter::BossCancels);
    g_machine.finish();
}

// The local machine starts a door out of a room it runs, with the other player inside: prepare the hand-over of the
// room's boss members.
void startOnDoor(const World& world) {
    if (!game_state::doorActive()) {
        g_doorLatched = false;
        return;
    }
    if (g_doorLatched || g_machine.inFlight() || !world.holder || !world.holder->here || !world.peerHere) return;
    g_doorLatched = true;
    Members members = collectMembers();
    if (members.empty()) return;
    if (anyLeased(members)) {
        logger::write("boss: not started, a member is in a lease");
        return;
    }
    g_lastToken = rule::nextToken(g_lastToken, character_owner::localSlot());
    g_members = std::move(members);
    g_scene = world.scene;
    g_epoch = world.holder->epoch;
    holdMembers();
    const auto count = static_cast<uint8_t>(g_members.size());
    wire::Header header = headerOf(g_lastToken, g_epoch, g_scene, wire::Phase::Prepare);
    header.count = count;
    header.alive = wire::maskOf(count);
    g_machine.startPrepare(g_lastToken, count, header.alive, world.nowMs);
    send(header, g_members);
    logPhase("prepare");
}

void onReady(const Arrival& arrival, const World& world) {
    const wire::Header& header = arrival.message.header;
    if (g_machine.role() != rule::Role::Old || header.scene != g_scene || header.roomEpoch != g_epoch || !g_machine.onReady(header.token, world.nowMs)) {
        logger::writeUnlessRepeated("boss: ready for token %u ignored", header.token);
        return;
    }
    send(headerOf(header.token, g_epoch, g_scene, wire::Phase::Commit));
    logPhase("commit");
}

// ---- Game thread: the new authority ------------------------------------------------------------------------------

void refuse(const wire::Header& request, wire::Reason reason) {
    sendCancel(request.token, request.roomEpoch, request.scene, reason);
    logCancel(request.token, reason);
    debug_stats::count(Counter::BossCancels);
}

bool fromHolder(const Arrival& arrival, const World& world) {
    return world.holder && !world.holder->here && arrival.sender == world.holder->slot;
}

void onPrepare(const Arrival& arrival, const World& world) {
    const wire::Header& header = arrival.message.header;
    if (!fromHolder(arrival, world)) return refuse(header, wire::Reason::Sender);
    if (header.scene != world.scene || header.roomEpoch != world.holder->epoch) return refuse(header, wire::Reason::Room);
    if (g_machine.inFlight()) return refuse(header, wire::Reason::Busy);
    if (!rule::acceptsPrepare(arrival.message.members, localsOf(arrival.message.members))) return refuse(header, wire::Reason::Member);
    g_members = arrival.message.members;
    g_scene = header.scene;
    g_epoch = header.roomEpoch;
    g_machine.acceptPrepare(header.token, header.count, header.alive, world.nowMs);
    send(headerOf(header.token, g_epoch, g_scene, wire::Phase::Ready));
    logPhase("ready");
}

void onCommit(const Arrival& arrival, const World& world) {
    const wire::Header& header = arrival.message.header;
    if (!fromHolder(arrival, world) || header.scene != g_scene || !g_machine.onCommit(header.token, world.nowMs)) {
        logger::writeUnlessRepeated("boss: commit for token %u ignored", header.token);
        return;
    }
    logPhase("commit");
}

// The transaction ended before the hand-over; the old authority carries on with its members, the new one drops its copy.
void onCancel(const Arrival& arrival) {
    const wire::Header& header = arrival.message.header;
    const bool old = g_machine.role() == rule::Role::Old;
    if (!g_machine.onCancel(header.token)) {
        logger::writeUnlessRepeated("boss: cancel for token %u ignored", header.token);
        return;
    }
    if (old) restartMembers();
    logCancel(header.token, static_cast<wire::Reason>(header.reason));
    debug_stats::count(Counter::BossCancels);
}

// The simulator's alive mask reached the machine that left the room.
void onOutcome(const Arrival& arrival) {
    const wire::Header& header = arrival.message.header;
    if (g_machine.role() != rule::Role::Old || !g_machine.owns(header.token) || header.scene != g_scene) {
        logger::writeUnlessRepeated("boss: outcome for token %u ignored", header.token);
        return;
    }
    const uint8_t alive = header.alive & wire::maskOf(g_machine.count());
    if (!g_machine.noteAlive(alive)) return;
    logPhase("outcome");
    if (alive == 0) g_machine.finish();
}

void drainInbox(const World& world) {
    std::vector<Arrival> arrivals;
    {
        std::lock_guard lock(g_mutex);
        arrivals.swap(g_inbox);
    }
    for (const Arrival& arrival : arrivals) {
        switch (static_cast<wire::Phase>(arrival.message.header.phase)) {
            case wire::Phase::Prepare: onPrepare(arrival, world); break;
            case wire::Phase::Ready: onReady(arrival, world); break;
            case wire::Phase::Commit: onCommit(arrival, world); break;
            case wire::Phase::Outcome: onOutcome(arrival); break;
            case wire::Phase::Cancel: onCancel(arrival); break;
        }
    }
}

// The members of the room another machine runs, noted a few times a second while it is in the room: what a machine that
// takes the room over alone commits from. The last notes are kept while the other player is already leaving.
void watch(const World& world) {
    const bool peerRuns = world.holder && !world.holder->here;
    const uint16_t heldScene = peerRuns ? world.scene : scene::kNone;
    if (heldScene != g_peerHeldScene) g_watched.clear();
    g_peerHeldScene = heldScene;
    if (!peerRuns || !world.together) return;
    if (!g_watched.empty() && world.nowMs - g_watchedMs < kWatchIntervalMs) return;
    g_watched = collectMembers();
    g_watchedMs = world.nowMs;
}

// The room changed hands to this machine (the enemy observer already carried its enemies on): put the offered snapshot
// back, or note the members it last saw, and run the fight from here.
void onFlip(const World& world) {
    const rule::Stage stage = g_machine.stage();
    const bool offered = g_machine.role() == rule::Role::New && g_scene == world.scene &&
                         (stage == rule::Stage::Prepared || stage == rule::Stage::Committed);
    if (!offered) {
        if (g_watched.empty()) return;
        g_members = std::move(g_watched);
        g_scene = world.scene;
    }
    g_epoch = world.holder->epoch;
    g_watched.clear();
    const std::vector<rule::Local> locals = localsOf(g_members);
    const rule::Plan plan = rule::planFlip(g_members, locals);
    const auto count = static_cast<uint8_t>(g_members.size());
    if (!offered) {
        g_lastToken = rule::nextToken(g_lastToken, character_owner::localSlot());
        g_machine.run(g_lastToken, count, plan.alive, world.nowMs);
        if (plan.verdict == rule::Verdict::Frozen) logger::write("boss: token %u member slot %u is gone", g_machine.token(), plan.badSlot);
        debug_stats::count(Counter::BossHandOvers);
        return logPhase("commit alone");
    }
    if (plan.verdict == rule::Verdict::Frozen) {
        g_machine.freeze(world.nowMs);
        holdMembers();
        resync::request("boss member missing");
        debug_stats::count(Counter::BossFrozen);
        logger::write("boss: token %u frozen (member slot %u, alive %02x)", g_machine.token(), plan.badSlot, plan.alive);
        return;
    }
    for (size_t i = 0; i < g_members.size(); ++i) {
        if (plan.resume & (1u << i)) enemy_observer::resume(g_members[i].slot, entryOf(g_members[i], rule::resumeHp(g_members[i].hp, locals[i].hp)));
    }
    const bool alone = stage == rule::Stage::Prepared;
    g_machine.run(g_machine.token(), count, plan.alive, world.nowMs);
    debug_stats::count(Counter::BossHandOvers);
    logPhase(alone ? "commit alone" : "applied");
}

// The new simulator tells the machine that left the room which members are alive.
void trackOutcome() {
    if (g_machine.role() != rule::Role::New || g_machine.stage() != rule::Stage::Running) return;
    const uint8_t alive = rule::aliveMask(g_members, localsOf(g_members));
    if (!g_machine.noteAlive(alive)) return;
    wire::Header header = headerOf(g_machine.token(), g_epoch, g_scene, wire::Phase::Outcome);
    header.alive = alive;
    send(header);
    logPhase("outcome");
    if (alive == 0) g_machine.finish();
}

// ---- Game thread: the transaction against the world --------------------------------------------------------------

// The loaded room is no longer the transaction's: its enemies are gone. An old authority that had committed has handed the
// room over and waits for the outcome; every other transaction ends.
void followRoom(const World& world) {
    if (!g_machine.active() || world.scene == g_scene || g_machine.stage() == rule::Stage::Away) return;
    switch (g_machine.stage()) {
        case rule::Stage::Committed:
            if (g_machine.role() == rule::Role::Old) {
                g_machine.leave(world.nowMs);
                return logPhase("handed over");
            }
            [[fallthrough]];
        case rule::Stage::Preparing:
        case rule::Stage::Prepared:
            logCancel(g_machine.token(), wire::Reason::Room);
            debug_stats::count(Counter::BossCancels);
            break;
        case rule::Stage::Running:
        case rule::Stage::Frozen:
            logPhase("ended");
            break;
        case rule::Stage::Idle:
        case rule::Stage::Away: break;
    }
    g_machine.finish();
}

// The link is gone: nothing can be handed over, and a frozen fight has nobody to wait for.
void followLink(const World& world) {
    if (world.linked || !g_machine.active()) return;
    switch (g_machine.stage()) {
        case rule::Stage::Preparing:
        case rule::Stage::Committed:
            if (g_machine.role() == rule::Role::Old) return cancelOld(wire::Reason::Left, false);
            logCancel(g_machine.token(), wire::Reason::Left);
            break;
        case rule::Stage::Prepared:
            logCancel(g_machine.token(), wire::Reason::Left);
            break;
        case rule::Stage::Frozen:
            restartMembers();
            logPhase("released");
            break;
        case rule::Stage::Running: return;
        case rule::Stage::Idle:
        case rule::Stage::Away: break;
    }
    g_machine.finish();
}

void followTimeouts(const World& world) {
    switch (g_machine.lapse(world.nowMs, !world.peerGone)) {
        case rule::Lapse::CancelPrepare:
        case rule::Lapse::CancelCommit:
            cancelOld(world.peerGone ? wire::Reason::Left : wire::Reason::Timeout, true);
            break;
        case rule::Lapse::DropPrepared:
        case rule::Lapse::DropCommit:
            logCancel(g_machine.token(), wire::Reason::Timeout);
            g_machine.finish();
            break;
        case rule::Lapse::None: break;
    }
}

void onTick() {
    const World world = readWorld();
    followRoom(world);
    drainInbox(world);
    if (g_peerHeldScene == world.scene && world.scene != scene::kNone && world.holder && world.holder->here) onFlip(world);
    followLink(world);
    followTimeouts(world);
    if (g_machine.inFlight() || g_machine.stage() == rule::Stage::Frozen) holdMembers();
    startOnDoor(world);
    watch(world);
    trackOutcome();
}

}  // namespace

namespace boss_handover {

bool enable() {
    if (!frame_router::add(wire::kMsgBossHandover, frame_router::SenderPolicy::Any, onFrame)) return false;
    game_tick::addCallback("enemy_boss_handover", onTick);
    return true;
}

}  // namespace boss_handover
