#include "enemy_lease.h"

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

#include "character_owner.h"
#include "contact_game.h"
#include "contact_rule.h"
#include "control_rule.h"
#include "debug_stats.h"
#include "enemy_decision.h"
#include "enemy_follow_rule.h"
#include "enemy_lease_rule.h"
#include "enemy_lease_wire.h"
#include "enemy_observer.h"
#include "enemy_observer_capture.h"
#include "enemy_observer_game.h"
#include "enemy_registry.h"
#include "frame_router.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "player_contact.h"
#include "pose_wire.h"
#include "puppet.h"
#include "room_authority.h"
#include "scene.h"
#include "state_net.h"
#include "stream_clock.h"

namespace {

namespace rule = contact_rule;
namespace table_rule = enemy_lease_rule;
namespace wire = enemy_lease_wire;
using character_owner::Character;
using debug_stats::Counter;
using enemy_follow_rule::Action;
using enemy_observer_wire::PoseEntry;

constexpr size_t kMaxInbox = 32;

struct LeaseArrival {
    uint8_t sender;
    wire::EnemyLease lease;
    uint32_t arrivalMs;
};
struct EndArrival {
    uint8_t sender;
    wire::EnemyLeaseEnd end;
};

// A paired contact the router saw: the grant is made once the enemy's update is over.
struct PendingContact {
    rule::Contact contact;
    uintptr_t enemy;
    int32_t state;
};

std::mutex g_mutex;
std::vector<LeaseArrival> g_leaseInbox;  // guarded by g_mutex: net thread to game thread
std::vector<EndArrival> g_endInbox;      // guarded by g_mutex

// Game thread only.
table_rule::LeaseTable g_table;
std::array<std::optional<PendingContact>, game::kEnemyPoolSlots> g_contacts;
std::vector<LeaseArrival> g_grants;  // received, waiting for their time on the authority's clock
uint32_t g_lastToken = 0;

template <typename Message>
void push(std::vector<Message>& inbox, Message&& message) {
    std::lock_guard lock(g_mutex);
    if (inbox.size() >= kMaxInbox) inbox.erase(inbox.begin());
    inbox.push_back(std::move(message));
}

void onLeaseFrame(const GameFrame& frame) {
    if (const auto lease = wire::decode<wire::EnemyLease>(frame.payload)) {
        return push(g_leaseInbox, LeaseArrival{frame.slot, *lease, pose_wire::clockMs()});
    }
    logger::writeUnlessRepeated("lease: malformed grant from slot %u refused", frame.slot);
}

void onEndFrame(const GameFrame& frame) {
    if (const auto end = wire::decode<wire::EnemyLeaseEnd>(frame.payload)) return push(g_endInbox, EndArrival{frame.slot, *end});
    logger::writeUnlessRepeated("lease: malformed end from slot %u refused", frame.slot);
}

std::optional<room_authority::Holder> loadedHolder() {
    const uint16_t loaded = scene::current();
    return loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
}

Action actionOf(const int32_t (&record)[4]) {
    Action action;
    for (size_t i = 0; i < enemy_follow_rule::kActionWords; ++i) action.word[i] = record[i];
    return action;
}

void copyRecord(const Action& action, int32_t (&record)[4]) {
    for (size_t i = 0; i < enemy_follow_rule::kActionWords; ++i) record[i] = action.word[i];
}

// The enemy in `slot` is the one the lease named.
bool boundEnemy(uint8_t slot, uint8_t classIndex, uint16_t spawnId, uintptr_t& enemy, PoseEntry& state) {
    enemy = enemy_registry::enemyAt(slot);
    return enemy && enemy_observer_capture::entryFor(slot, enemy, state) && state.classIndex == classIndex && state.spawnId == spawnId;
}

// ---- Ending ------------------------------------------------------------------------------------------------------

void sendEnd(uint32_t token, uint8_t slot, rule::End reason, const PoseEntry& state) {
    const std::optional<room_authority::Holder> holder = loadedHolder();
    if (!holder) return;
    wire::EnemyLeaseEnd end{};
    end.token = token;
    end.roomEpoch = holder->epoch;
    end.scene = scene::current();
    end.slot = slot;
    end.reason = static_cast<uint8_t>(reason);
    end.hp = state.hp;
    for (int i = 0; i < 4; ++i) end.record[i] = state.record[i];
    for (int i = 0; i < 3; ++i) end.pos[i] = state.pos[i];
    end.quat = state.quat;
    state_net::send(wire::kMsgEnemyLeaseEnd, true, proto::bytesOf(end));
}

PoseEntry grantState(const wire::EnemyLease& grant) {
    PoseEntry state{};
    state.slot = grant.slot;
    state.classIndex = grant.classIndex;
    state.spawnId = grant.spawnId;
    state.hp = grant.hp;
    for (int i = 0; i < 4; ++i) state.record[i] = grant.record[i];
    for (int i = 0; i < 3; ++i) state.pos[i] = grant.pos[i];
    state.quat = grant.quat;
    return state;
}

// The state a lease was granted in, which is what the authority resumes from when the holder's own state is lost.
PoseEntry grantedState(const table_rule::Lease& lease) {
    PoseEntry state{};
    state.slot = lease.slot;
    state.classIndex = lease.classIndex;
    state.spawnId = lease.spawnId;
    state.hp = lease.hp;
    for (int i = 0; i < 4; ++i) state.record[i] = lease.granted.word[i];
    for (int i = 0; i < 3; ++i) state.pos[i] = lease.lastPos[i];
    state.quat = lease.quat;
    return state;
}

// The authority takes its enemy back: from the holder's final state when the hold ran, otherwise from the state it last
// showed. The lease is already out of the table, so the enemy leaves the stream scope on the next frame.
void resumeAway(const table_rule::Lease& lease, rule::End reason, const PoseEntry* holderState) {
    debug_stats::count(Counter::LeasesEnded);
    std::optional<PoseEntry> state = enemy_observer::snapshot(lease.slot);
    if (holderState && rule::carriesState(reason)) state = *holderState;
    if (state && enemy_observer::resume(lease.slot, *state)) {
        logger::write("lease: token %u ended (%s) hp %d", lease.token, rule::name(reason), state->hp);
    } else {
        logger::write("lease: token %u ended (%s), enemy not resumed", lease.token, rule::name(reason));
    }
}

void applyEnd(const EndArrival& arrival) {
    const wire::EnemyLeaseEnd& end = arrival.end;
    const table_rule::Lease* lease = g_table.find(end.slot);
    const std::optional<room_authority::Holder> holder = loadedHolder();
    if (!lease || lease->token != end.token || lease->peer != arrival.sender || end.scene != scene::current() || !holder ||
        end.roomEpoch != holder->epoch) {
        logger::writeUnlessRepeated("lease: end of token %u for slot %u from slot %u ignored", end.token, end.slot, arrival.sender);
        return;
    }
    const table_rule::Lease ended = *g_table.remove(end.slot, end.token);
    const rule::End reason = static_cast<rule::End>(end.reason);
    if (ended.role == table_rule::Role::Held) {
        debug_stats::count(Counter::LeasesEnded);
        logger::write("lease: token %u ended (%s by the authority)", ended.token, rule::name(reason));
        return;
    }
    PoseEntry holderState = grantedState(ended);
    holderState.hp = end.hp;
    for (int i = 0; i < 4; ++i) holderState.record[i] = end.record[i];
    for (int i = 0; i < 3; ++i) holderState.pos[i] = end.pos[i];
    holderState.quat = end.quat;
    resumeAway(ended, reason, &holderState);
}

// ---- The authority: contacts become grants -----------------------------------------------------------------------

void grant(const PendingContact& pending, uint32_t nowMs) {
    const rule::Contact& contact = pending.contact;
    PoseEntry enemy{};
    const uintptr_t current = enemy_registry::enemyAt(contact.slot);
    if (current != pending.enemy || !enemy_observer_capture::entryFor(contact.slot, current, enemy)) return;  // the slot changed hands
    const std::optional<room_authority::Holder> holder = loadedHolder();
    const int owner = character_owner::ownerSlot(static_cast<Character>(contact.victim));
    const Action record = actionOf(enemy.record);
    const bool simulates = holder && holder->here && g_table.roleOf(contact.slot) == table_rule::Role::None;
    if (!simulates || owner == control_rule::kNoOwner || !rule::grantable(enemy.hp, record) || !wire::recordFits(enemy.record)) {
        logger::write("lease: slot %u not granted (hp %d, record %d,%d)", contact.slot, enemy.hp, enemy.record[0], enemy.record[1]);
        return;
    }
    wire::EnemyLease message{};
    g_lastToken = table_rule::nextToken(g_lastToken);
    message.token = g_lastToken;
    message.roomEpoch = holder->epoch;
    message.senderMs = nowMs;
    message.scene = scene::current();
    message.slot = contact.slot;
    message.classIndex = enemy.classIndex;
    message.spawnId = enemy.spawnId;
    message.victim = contact.victim;
    message.kind = contact.kind;
    message.level = contact.level;
    message.hp = enemy.hp;
    for (int i = 0; i < 4; ++i) message.record[i] = enemy.record[i];
    for (int i = 0; i < 3; ++i) {
        message.pos[i] = enemy.pos[i];
        message.point[i] = contact.point[i];
    }
    message.quat = enemy.quat;
    if (!state_net::send(wire::kMsgEnemyLease, true, proto::bytesOf(message))) return;
    table_rule::Lease lease;
    lease.token = message.token;
    lease.slot = contact.slot;
    lease.peer = static_cast<uint8_t>(owner);
    lease.victim = contact.victim;
    lease.classIndex = enemy.classIndex;
    lease.spawnId = enemy.spawnId;
    lease.role = table_rule::Role::Away;
    lease.granted = record;
    lease.hp = enemy.hp;
    for (int i = 0; i < 3; ++i) lease.lastPos[i] = enemy.pos[i];
    lease.startedMs = nowMs;
    g_table.add(lease);
    debug_stats::count(Counter::LeasesGranted);
    logger::write("lease: token %u slot %u to slot %d (%s)", lease.token, lease.slot, owner, enemy_policy::nameOf(enemy.classIndex));
}

void settleContacts(uint32_t nowMs) {
    for (std::optional<PendingContact>& place : g_contacts) {
        if (!place) continue;
        const PendingContact pending = *place;
        place.reset();
        grant(pending, nowMs);
    }
}

void expireAway(uint32_t nowMs) {
    for (const table_rule::Lapsed& lapsed : g_table.lapsed(nowMs)) {
        const std::optional<PoseEntry> state = enemy_observer::snapshot(lapsed.slot);
        if (state) sendEnd(lapsed.token, lapsed.slot, lapsed.reason, *state);
        table_rule::Lease ended;
        ended.token = lapsed.token;
        ended.slot = lapsed.slot;
        resumeAway(ended, lapsed.reason, nullptr);
    }
}

// ---- The holder: grants become native holds ----------------------------------------------------------------------

void decline(const wire::EnemyLease& grant, const char* why) {
    sendEnd(grant.token, grant.slot, rule::End::Declined, grantState(grant));
    logger::write("lease: token %u declined (%s)", grant.token, why);
}

// The grant comes from the room's authority, names this room and epoch, and its victim is a character this machine owns.
const char* refusalOf(const wire::EnemyLease& grant, uint8_t sender) {
    const std::optional<room_authority::Holder> holder = loadedHolder();
    if (grant.scene != scene::current() || !holder || grant.roomEpoch != holder->epoch) return "room";
    if (holder->here || holder->slot != sender) return "sender";
    if (!character_owner::isLocalOwned(static_cast<Character>(grant.victim))) return "victim";
    if (g_table.roleOf(grant.slot) != table_rule::Role::None) return "already leased";
    return nullptr;
}

void takeGrant(const LeaseArrival& arrival) {
    if (const char* why = refusalOf(arrival.lease, arrival.sender)) return decline(arrival.lease, why);
    g_grants.push_back(arrival);
}

// The enemy visibly reaches the victim: place the local copy at the grant pose, let the engine's own damage entry bind
// the victim to it, then run the granted action natively.
void startGrant(const LeaseArrival& arrival, uint32_t nowMs) {
    const wire::EnemyLease& grant = arrival.lease;
    uintptr_t enemy = 0;
    PoseEntry local{};
    const uintptr_t victim = character_owner::find(static_cast<Character>(grant.victim));
    if (const char* why = refusalOf(grant, arrival.sender)) return decline(grant, why);
    if (!boundEnemy(grant.slot, grant.classIndex, grant.spawnId, enemy, local)) return decline(grant, "enemy");
    if (!victim || !game_state::inCurrentRoom(victim)) return decline(grant, "victim not in the room");
    float quat[4];
    pose_wire::unpackQuat(grant.quat, quat);
    enemy_observer_game::place(enemy, grant.pos, quat, grant.hp);
    const float point[3] = {grant.pos[0] + grant.point[0], grant.pos[1] + grant.point[1], grant.pos[2] + grant.point[2]};
    if (!player_contact::applyToLocal(victim, enemy, point, grant.kind, grant.level)) return decline(grant, "victim refused");
    table_rule::Lease lease;
    lease.token = grant.token;
    lease.slot = grant.slot;
    lease.peer = arrival.sender;
    lease.victim = grant.victim;
    lease.classIndex = grant.classIndex;
    lease.spawnId = grant.spawnId;
    lease.role = table_rule::Role::Held;
    lease.granted = actionOf(grant.record);
    lease.hp = grant.hp;
    for (int i = 0; i < 3; ++i) lease.lastPos[i] = grant.pos[i];
    lease.quat = grant.quat;
    lease.startedMs = nowMs;
    g_table.add(lease);
    enemy_decision::startAction(enemy, enemy_follow_rule::startOf(lease.granted));
    debug_stats::count(Counter::LeasesStarted);
    logger::write("lease: token %u started here (%s held)", lease.token, enemy_policy::nameOf(grant.classIndex));
}

void startDueGrants(uint32_t nowMs) {
    for (size_t i = 0; i < g_grants.size();) {
        const LeaseArrival arrival = g_grants[i];
        const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(arrival.sender);
        const bool lapsed = rule::pendingLapsed(nowMs - arrival.arrivalMs);
        const bool due = clock && !stream_clock::after(arrival.lease.senderMs, clock->target(nowMs));
        if (!lapsed && !due) {
            ++i;
            continue;
        }
        g_grants.erase(g_grants.begin() + static_cast<std::ptrdiff_t>(i));
        if (lapsed && !due) {
            decline(arrival.lease, "expired");
        } else {
            startGrant(arrival, nowMs);
        }
    }
}

// Why a lease this machine holds is over, with the enemy's state to send back.
rule::End heldEnded(const table_rule::Lease& lease, uint32_t nowMs, PoseEntry& state) {
    uintptr_t enemy = 0;
    const uintptr_t victim = character_owner::find(static_cast<Character>(lease.victim));
    rule::VictimView view;
    if (!boundEnemy(lease.slot, lease.classIndex, lease.spawnId, enemy, state) || !victim || !contact_game::victimView(victim, view)) {
        return rule::End::Left;
    }
    return rule::finished(view, actionOf(state.record), lease.granted, state.hp, nowMs - lease.startedMs);
}

void watchHeld(uint32_t nowMs) {
    for (uint8_t slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const table_rule::Lease* lease = g_table.find(slot);
        if (!lease || lease->role != table_rule::Role::Held) continue;
        PoseEntry state{};
        const rule::End reason = heldEnded(*lease, nowMs, state);
        if (reason == rule::End::None) continue;
        if (reason == rule::End::Left) state = grantedState(*lease);
        sendEnd(lease->token, slot, reason, state);
        debug_stats::count(Counter::LeasesEnded);
        logger::write("lease: token %u ended (%s) hp %d", lease->token, rule::name(reason), state.hp);
        g_table.remove(slot, lease->token);
    }
}

void drainInboxes() {
    std::vector<LeaseArrival> leases;
    std::vector<EndArrival> ends;
    {
        std::lock_guard lock(g_mutex);
        leases.swap(g_leaseInbox);
        ends.swap(g_endInbox);
    }
    for (const LeaseArrival& arrival : leases) takeGrant(arrival);
    for (const EndArrival& arrival : ends) applyEnd(arrival);
}

void onTick() {
    const uint32_t nowMs = pose_wire::clockMs();
    settleContacts(nowMs);
    drainInboxes();
    startDueGrants(nowMs);
    watchHeld(nowMs);
    expireAway(nowMs);
}

}  // namespace

namespace enemy_lease {

bool enable() {
    if (!frame_router::add(wire::kMsgEnemyLease, frame_router::SenderPolicy::Any, onLeaseFrame) ||
        !frame_router::add(wire::kMsgEnemyLeaseEnd, frame_router::SenderPolicy::Any, onEndFrame)) {
        return false;
    }
    game_tick::addCallback("enemy_lease", onTick);
    return true;
}

Role roleOf(uint8_t slot) { return g_table.roleOf(slot); }

std::optional<uint8_t> peerOf(uint8_t slot) {
    const table_rule::Lease* lease = g_table.find(slot);
    return lease ? std::optional<uint8_t>(lease->peer) : std::nullopt;
}

bool anyAway() { return g_table.any(Role::Away); }

bool anyHeld() { return g_table.any(Role::Held); }

uint16_t tokenOf(uint8_t slot) {
    const table_rule::Lease* lease = g_table.find(slot);
    return lease && lease->role == Role::Held ? static_cast<uint16_t>(lease->token & 0xffff) : 0;
}

bool busy(uint8_t slot) { return slot < g_contacts.size() && (g_contacts[slot] || g_table.find(slot)); }

bool busyWith(uint8_t slot, uint8_t victim) {
    if (slot >= g_contacts.size()) return false;
    if (g_contacts[slot]) return g_contacts[slot]->contact.victim == victim;
    const table_rule::Lease* lease = g_table.find(slot);
    return lease && lease->victim == victim;
}

void request(const rule::Contact& contact, uintptr_t enemy, int32_t state) {
    if (contact.slot < g_contacts.size()) g_contacts[contact.slot] = PendingContact{contact, enemy, state};
}

bool acceptPose(const PoseEntry& entry, uint8_t sender, uint32_t nowMs) {
    const table_rule::Lease* lease = g_table.find(entry.slot);
    if (!lease || lease->role != Role::Away || lease->peer != sender) {
        debug_stats::count(Counter::LeasePosesRefused);
        logger::writeUnlessRepeated("lease: pose for slot %u from slot %u refused (not the holder)", entry.slot, sender);
        return false;
    }
    const table_rule::PoseVerdict verdict = g_table.offerPose(entry.slot, entry.leaseToken, entry.pos, entry.hp, nowMs);
    if (verdict == table_rule::PoseVerdict::Accepted) return true;
    debug_stats::count(Counter::LeasePosesRefused);
    logger::writeUnlessRepeated("lease: pose for slot %u refused (%s)", entry.slot, table_rule::name(verdict));
    return false;
}

void reset() {
    g_table.clear();
    g_contacts = {};
    g_grants.clear();
    std::lock_guard lock(g_mutex);
    g_leaseInbox.clear();
    g_endInbox.clear();
}

}  // namespace enemy_lease
