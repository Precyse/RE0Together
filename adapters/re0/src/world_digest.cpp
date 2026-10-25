#include "world_digest.h"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "flag_diff.h"
#include "flag_sequencer.h"
#include "flag_sync.h"
#include "floor_items_sync.h"
#include "frame_router.h"
#include "game_tick.h"
#include "join_sync.h"
#include "log.h"
#include "resync.h"
#include "room_authority.h"
#include "scene.h"
#include "state_net.h"
#include "world_digest_rule.h"
#include "world_digest_wire.h"

namespace {

namespace rule = world_digest_rule;
namespace wire = world_digest_wire;

constexpr uint32_t kSnapshotIntervalMs = 250;  // the game tick reads the stores this often
constexpr uint32_t kSendIntervalMs = 1000;

std::mutex g_mutex;
wire::WorldDigest g_local{};  // guarded by g_mutex: the newest snapshot, game thread to net thread
bool g_hasLocal = false;      // guarded by g_mutex
rule::Canary g_canary;        // guarded by g_mutex
uint32_t g_lastSnapshotMs = 0;  // game thread
uint32_t g_lastSendMs = 0;      // net thread

template <class T>
T saturate(size_t value) {
    return static_cast<T>(std::min<size_t>(value, std::numeric_limits<T>::max()));
}

bool read(wire::WorldDigest& out) {
    flag_diff::Words words;
    if (!flag_sync::read(words)) return false;
    const rule::FlagCrcs crcs = rule::flagCrcs(words);
    out = {};
    out.flagSerial = flag_sequencer::appliedSerial();
    out.flagCrc = crcs.story;
    out.enemyFlagCrc = crcs.enemy;
    out.actFlagCrc = crcs.act;
    out.authorityHash = room_authority::digest();
    out.floorJournal = saturate<uint16_t>(floor_items_sync::journalSize());
    out.loadSerial = scene::loadSerial();
    out.pendingProposals = saturate<uint8_t>(flag_sequencer::pendingCount());
    out.flags = join_sync::caughtUp() ? wire::kCaughtUp : 0;
    out.scene = scene::current();
    return true;
}

void onTick() {
    const uint32_t now = GetTickCount();
    if (now - g_lastSnapshotMs < kSnapshotIntervalMs) return;
    g_lastSnapshotMs = now;
    wire::WorldDigest digest;
    const bool ok = read(digest);
    std::lock_guard lock(g_mutex);
    g_hasLocal = ok;
    if (ok) g_local = digest;
}

// The store of `verdict` differs on the two machines: tell the log and ask for it again.
void repair(const rule::Verdict& verdict, uint32_t serial) {
    const char* scope = rule::name(verdict.scope);
    logger::write("world_digest: diverged (%s) at serial %u", scope, serial);
    debug_stats::count(debug_stats::Counter::WorldDigestsDiverged);
    if (verdict.scope != rule::Scope::Authority) {
        resync::request((std::string("world digest: ") + scope).c_str());
        return;
    }
    if (verdict.repeat) resync::request("world digest: authority");
    else room_authority::resendClaims();
}

void onDigest(const GameFrame& frame) {
    const auto peer = wire::decode(frame.payload);
    if (!peer) {
        logger::writeUnlessRepeated("world_digest: refused a malformed digest from slot %d", frame.slot);
        return;
    }
    std::vector<rule::Verdict> verdicts;
    uint32_t serial = 0;
    {
        std::lock_guard lock(g_mutex);
        if (!g_hasLocal) return;
        serial = g_local.flagSerial;
        verdicts = g_canary.compare(g_local, *peer, GetTickCount());
    }
    for (const rule::Verdict& verdict : verdicts) {
        if (verdict.event == rule::Event::Diverged) repair(verdict, serial);
        else logger::write("world_digest: converged (%s)", rule::name(verdict.scope));
    }
}

}  // namespace

namespace world_digest {

bool enable() {
    if (!frame_router::add(wire::kMsgWorldDigest, frame_router::SenderPolicy::Any, onDigest)) return false;
    game_tick::addCallback("world_digest", onTick);
    return true;
}

void onNetTick() {
    const uint32_t now = GetTickCount();
    if (now - g_lastSendMs < kSendIntervalMs) return;
    g_lastSendMs = now;
    wire::WorldDigest digest;
    {
        std::lock_guard lock(g_mutex);
        if (!g_hasLocal) return;
        digest = g_local;
    }
    digest.senderMs = now;
    if (state_net::send(wire::kMsgWorldDigest, false, proto::bytesOf(digest))) {
        debug_stats::count(debug_stats::Counter::WorldDigestsSent);
    }
}

}  // namespace world_digest
