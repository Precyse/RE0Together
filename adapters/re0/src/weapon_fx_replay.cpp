#include "weapon_fx_replay.h"

#include <bitset>
#include <mutex>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "frame_router.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "puppet.h"
#include "room_phase.h"
#include "timed_queue.h"
#include "weapon_fx_game.h"
#include "weapon_fx_sites.h"
#include "weapon_fx_wire.h"

namespace {

using debug_stats::Counter;
using weapon_fx_wire::Batch;
using weapon_fx_wire::Entry;

constexpr int kNoSlot = -1;

std::mutex g_mutex;
timed_queue::TimedQueue<Batch> g_queue;  // guarded by g_mutex
int g_slot = kNoSlot;                    // guarded: the peer whose batches are queued

// Game thread only.
std::vector<Batch> g_due;
bool g_hasGeneration = false;
uint32_t g_generation = 0;  // of the stream clock the queued batches were stamped against
std::bitset<weapon_fx_sites::kSites.size()> g_replayedSites;

// Net thread: WEAPON_FX from the driving peer.
void onFrame(const GameFrame& frame) {
    if (static_cast<int>(frame.slot) != net_pad::peerSlot()) return;
    auto batch = weapon_fx_wire::decode(frame.payload);
    if (!batch) {
        logger::writeUnlessRepeated("weapon_fx: refused a malformed message from slot %u", frame.slot);
        return;
    }
    debug_stats::count(Counter::WeaponFxIn, static_cast<uint32_t>(batch->entries.size()));
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(frame.slot) != g_slot) {
        g_queue.clear();
        g_slot = frame.slot;
    }
    g_queue.push(*batch);
}

// A door, a cutscene or another room: the effects belong to a scene this machine does not show.
bool replayable(uintptr_t player) {
    return !room_phase::isEvent(game_state::roomPhase()) && !game_state::doorActive() && game_state::inCurrentRoom(player);
}

// The batches the peer's clock has reached; the queue starts over when that clock was re-anchored.
void takeDue(const stream_clock::StreamClock& clock) {
    std::lock_guard lock(g_mutex);
    if (g_hasGeneration && clock.generation() != g_generation) g_queue.clear();
    g_hasGeneration = true;
    g_generation = clock.generation();
    const uint32_t stale = g_queue.takeDue(clock.target(pose_wire::clockMs()), g_due);
    if (stale > 0) logger::writeUnlessRepeated("weapon_fx: %u stale messages dropped", stale);
}

void replayEntry(uintptr_t player, const Entry& entry) {
    const char* who = character_owner::name(character_owner::identify(player));
    if (weapon_fx_wire::isProjectile(entry)) {
        const int32_t here = weapon_fx_game::weaponTypeOf(player);
        if (here != static_cast<int32_t>(entry.weapon)) {
            logger::writeUnlessRepeated("weapon_fx: %s projectile of weapon %u, puppet holds %d", who, entry.weapon, here);
        }
    }
    if (!weapon_fx_game::replay(player, entry)) {
        logger::writeUnlessRepeated("weapon_fx: %s %s site %u failed", who, weapon_fx_wire::name(entry.op), entry.site);
    } else if (!g_replayedSites.test(entry.site)) {
        g_replayedSites.set(entry.site);
        logger::write("weapon_fx: %s replayed %s site %u", who, weapon_fx_wire::name(entry.op), entry.site);
    }
}

// Game thread, after each character's own move: the puppet's due effects, in the owner's order.
void afterMove(uintptr_t player) {
    if (!puppet::active(player)) return;
    int slot;
    {
        std::lock_guard lock(g_mutex);
        slot = g_slot;
    }
    if (slot == kNoSlot) return;
    const auto clock = puppet::clockFor(static_cast<uint8_t>(slot));
    if (!clock) return;
    g_due.clear();
    takeDue(*clock);
    if (!replayable(player)) return;
    for (const Batch& batch : g_due) {
        for (const Entry& entry : batch.entries) replayEntry(player, entry);
    }
}

}  // namespace

namespace weapon_fx_replay {

bool enable() {
    if (!frame_router::add(weapon_fx_wire::kMsgWeaponFx, frame_router::SenderPolicy::Any, onFrame)) return false;
    game_tick::addPostMove("weapon_fx_replay", afterMove);
    return true;
}

}  // namespace weapon_fx_replay
