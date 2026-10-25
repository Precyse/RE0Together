#include "contact_net.h"

#include <mutex>
#include <optional>
#include <vector>

#include "character_owner.h"
#include "contact_game.h"
#include "debug_stats.h"
#include "enemy_lease_wire.h"
#include "enemy_observer.h"
#include "enemy_observer_capture.h"
#include "enemy_registry.h"
#include "frame_router.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "player_contact.h"
#include "position_blend.h"
#include "pose_wire.h"
#include "puppet.h"
#include "room_authority.h"
#include "scene.h"
#include "state_net.h"
#include "timed_queue.h"

namespace {

namespace wire = enemy_lease_wire;
using character_owner::Character;
using debug_stats::Counter;

constexpr size_t kMaxInbox = 64;  // hits waiting for the game thread

struct Arrival {
    uint8_t sender;
    wire::PlayerHit hit;
};

// What the timed queue orders: the sender's clock and numbering.
struct Due {
    uint32_t senderMs;
    uint32_t seq;
    wire::PlayerHit hit;
};

std::mutex g_mutex;
std::vector<Arrival> g_inbox;  // guarded by g_mutex: net thread to game thread

// Game thread only.
timed_queue::TimedQueue<Due> g_queue;
int g_queueSender = -1;
std::vector<Due> g_due;
uint32_t g_sendSeq = 0;

void onFrame(const GameFrame& frame) {
    const auto hit = wire::decode<wire::PlayerHit>(frame.payload);
    if (!hit) {
        logger::writeUnlessRepeated("contact: malformed hit from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_inbox.size() >= kMaxInbox) g_inbox.erase(g_inbox.begin());
    g_inbox.push_back({frame.slot, *hit});
}

// The hit names this room, the table's epoch and an enemy whose simulator is the sender, and its victim is a character
// this machine owns.
bool admissible(const wire::PlayerHit& hit, uint8_t sender) {
    const uint16_t loaded = scene::current();
    const std::optional<room_authority::Holder> holder = loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
    const enemy_observer_rule::Simulator simulator = enemy_observer::simulatorOf(hit.slot);
    const Character victim = static_cast<Character>(hit.victim);
    return hit.scene == loaded && holder && hit.roomEpoch == holder->epoch && simulator.known && !simulator.here &&
           simulator.slot == sender && character_owner::isLocalOwned(victim);
}

void take(const Arrival& arrival) {
    if (!admissible(arrival.hit, arrival.sender)) {
        logger::writeUnlessRepeated("contact: hit for slot %u from slot %u dropped (not the simulator, room or victim)", arrival.hit.slot,
                                    arrival.sender);
        debug_stats::count(Counter::ContactHitsDropped);
        return;
    }
    if (g_queueSender != arrival.sender) {
        g_queue.clear();
        g_queueSender = arrival.sender;
    }
    g_queue.push({arrival.hit.senderMs, arrival.hit.seq, arrival.hit});
}

// The enemy in the hit's slot is this room's enemy of the hit's class and spawn.
std::optional<uintptr_t> boundEnemy(const wire::PlayerHit& hit) {
    const uintptr_t enemy = enemy_registry::enemyAt(hit.slot);
    enemy_observer_wire::PoseEntry local{};
    if (!enemy || !enemy_observer_capture::entryFor(hit.slot, enemy, local) || local.classIndex != hit.classIndex ||
        local.spawnId != hit.spawnId) {
        return std::nullopt;
    }
    return enemy;
}

// The engine's own damage entry, with the local copy of the enemy as the attacker.
void apply(const wire::PlayerHit& hit) {
    const Character victimId = static_cast<Character>(hit.victim);
    const uintptr_t victim = character_owner::find(victimId);
    const std::optional<uintptr_t> enemy = boundEnemy(hit);
    contact_rule::VictimView view;
    if (hit.scene != scene::current() || !victim || !enemy || !game_state::inCurrentRoom(victim) ||
        !contact_game::victimView(victim, view) || view.hp <= 0) {
        debug_stats::count(Counter::ContactHitsDropped);
        return;
    }
    float enemyPos[3];
    if (!game::readMemory(*enemy + game::kUnitPositionOffset, enemyPos)) return;
    const float point[3] = {enemyPos[0] + hit.point[0], enemyPos[1] + hit.point[1], enemyPos[2] + hit.point[2]};
    float victimPos[3];
    if (!game::readMemory(victim + game::kUnitPositionOffset, victimPos)) return;
    if (!contact_rule::hitReaches(point, victimPos)) {
        debug_stats::count(Counter::ContactHitsDropped);
        logger::write("contact: hit on %s from local slot %u refused, it landed %.0f units from where %s stands",
                      character_owner::name(victimId), hit.slot, position_blend::distance(point, victimPos), character_owner::name(victimId));
        return;
    }
    if (!player_contact::applyToLocal(victim, *enemy, point, hit.kind, hit.level)) {
        debug_stats::count(Counter::ContactHitsDropped);
        return;
    }
    debug_stats::count(Counter::ContactHitsApplied);
    logger::write("contact: hit applied to %s from local slot %u", character_owner::name(victimId), hit.slot);
}

void onTick() {
    std::vector<Arrival> arrivals;
    {
        std::lock_guard lock(g_mutex);
        arrivals.swap(g_inbox);
    }
    for (Arrival& arrival : arrivals) {
        if (!enemy_observer::toLocalSlot(arrival.hit.slot, arrival.hit.spawnRecord)) {
            logger::writeUnlessRepeated("contact: hit names record %u, no enemy of it here", arrival.hit.spawnRecord);
            debug_stats::count(Counter::ContactHitsDropped);
            continue;
        }
        take(arrival);
    }
    if (g_queueSender < 0) return;
    const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(static_cast<uint8_t>(g_queueSender));
    if (!clock) return;
    g_due.clear();
    g_queue.takeDue(clock->target(pose_wire::clockMs()), g_due);
    for (const Due& due : g_due) apply(due.hit);
}

}  // namespace

namespace contact_net {

bool enable() {
    if (!frame_router::add(wire::kMsgPlayerHit, frame_router::SenderPolicy::Any, onFrame)) return false;
    game_tick::addCallback("contact_net", onTick);
    return true;
}

void sendHit(const contact_rule::Contact& contact, const enemy_observer_wire::PoseEntry& enemy) {
    const uint16_t loaded = scene::current();
    const std::optional<room_authority::Holder> holder = loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
    if (!holder) return;
    wire::PlayerHit hit{};
    hit.roomEpoch = holder->epoch;
    hit.senderMs = pose_wire::clockMs();
    hit.seq = ++g_sendSeq;
    hit.scene = loaded;
    hit.slot = contact.slot;
    hit.classIndex = enemy.classIndex;
    hit.spawnId = enemy.spawnId;
    hit.spawnRecord = enemy.spawnRecord;
    hit.victim = contact.victim;
    hit.kind = contact.kind;
    hit.level = contact.level;
    for (int i = 0; i < 3; ++i) hit.point[i] = contact.point[i];
    if (state_net::send(wire::kMsgPlayerHit, true, proto::bytesOf(hit))) debug_stats::count(Counter::ContactHitsSent);
}

}  // namespace contact_net
