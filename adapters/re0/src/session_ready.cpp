#include "session_ready.h"

#include <atomic>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "frame_router.h"
#include "game_tick.h"
#include "join_extras.h"
#include "join_sync.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "puppet.h"
#include "scene.h"
#include "session_ready_rule.h"
#include "state_net.h"

namespace {

namespace rule = session_ready_rule;
using debug_stats::Counter;
using session_wire::ReadyPhase;
using session_wire::SessionReady;

std::mutex g_mutex;
std::optional<SessionReady> g_echo;  // guarded by g_mutex: net thread to game thread, guest only
rule::ReadyClient g_client;          // game thread, guest only
std::atomic<bool> g_echoed{false};

// Host: the guest says where it stands. Accepted when the guest's own newest pose says the same.
void onReady(uint8_t slot, const SessionReady& ready) {
    if (!character_owner::isHost() || static_cast<int>(slot) != net_pad::peerSlot()) {
        logger::writeUnlessRepeated("session_ready: ready from slot %u ignored", slot);
        return;
    }
    const rule::Verdict verdict =
        rule::judge(ready, puppet::latestPose(slot), join_extras::lastSerial(), pose_wire::clockMs());
    if (verdict != rule::Verdict::Accept) {
        debug_stats::count(Counter::SessionReadyRefused);
        logger::writeUnlessRepeated("session_ready: refused (%s) scene 0x%02x serial %u", rule::name(verdict), ready.scene,
                                    ready.snapshotSerial);
        return;
    }
    const SessionReady echo = rule::echoOf(ready);
    if (state_net::send(session_wire::kMsgSessionReady, true, proto::bytesOf(echo))) {
        logger::write("session_ready: echoed (scene 0x%02x serial %u)", ready.scene, ready.snapshotSerial);
    }
}

// Net thread, through the frame router.
void onFrame(const GameFrame& frame) {
    const auto message = session_wire::decodeReady(frame.payload);
    if (!message) {
        logger::writeUnlessRepeated("session_ready: refused a malformed message from slot %d", frame.slot);
        return;
    }
    if (message->phase == static_cast<uint8_t>(ReadyPhase::Ready)) {
        onReady(frame.slot, *message);
        return;
    }
    if (character_owner::isHost() || !character_owner::isHostSlot(frame.slot)) return;
    std::lock_guard lock(g_mutex);
    g_echo = *message;
}

std::optional<SessionReady> takeEcho() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_echo, std::nullopt);
}

// Guest, game thread: arms the handshake when a snapshot with its extras was applied, counts the echo, and asks again until it
// comes.
void onTick() {
    if (!net_pad::active() || character_owner::isHost()) return;
    if (const auto serial = join_extras::applyArrived()) g_client.arm(*serial);
    if (const auto echo = takeEcho(); echo && g_client.onEcho(*echo)) {
        g_echoed = true;
        debug_stats::count(Counter::SessionReadyEchoes);
        logger::write("session_ready: echo received (scene 0x%02x serial %u)", echo->scene, echo->snapshotSerial);
    }
    if (!join_sync::placed()) return;
    const auto ready = g_client.next(pose_wire::clockMs(), scene::current(), scene::loadSerial());
    if (!ready || !state_net::send(session_wire::kMsgSessionReady, true, proto::bytesOf(*ready))) return;
    debug_stats::count(Counter::SessionReadySent);
    logger::write("session_ready: sent (scene 0x%02x serial %u)", ready->scene, ready->snapshotSerial);
}

}  // namespace

namespace session_ready {

bool enable() {
    if (!frame_router::add(session_wire::kMsgSessionReady, frame_router::SenderPolicy::Any, onFrame)) return false;
    game_tick::addCallback("session_ready", onTick);
    return true;
}

bool echoed() { return g_echoed.load(); }

void reset() {
    g_client.reset();
    g_echoed = false;
    std::lock_guard lock(g_mutex);
    g_echo.reset();
}

}  // namespace session_ready
