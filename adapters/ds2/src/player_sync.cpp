#include "player_sync.h"

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "anim_event.h"
#include "anim_sync.h"
#include "authority_sync.h"
#include "cargo_ground.h"
#include "cargo_transfer.h"
#include "clock_sync.h"
#include "debug_stats.h"
#include "partner_cargo_sync.h"
#include "story_sync.h"
#include "rack_sync.h"
#include "struct_sync.h"
#include "bt_sync.h"
#include "env_sync.h"
#include "enemy_combat.h"
#include "camp_sync.h"
#include "enemy_sync.h"
#include "equip_sync.h"
#include "fact_sync.h"
#include "game.h"
#include "log.h"
#include "position_blend.h"
#include "reject_counters.h"
#include "resync.h"
#include "resync_trigger.h"
#include "toast_queue.h"
#include "vehicle_load.h"
#include "vehicle_sync.h"
#include "weapon_sync.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kGameId = "ds2";
constexpr auto kSendInterval = std::chrono::microseconds(static_cast<int>(1'000'000 / player_sync::kSendHz));
constexpr auto kStaleAfter = std::chrono::seconds(3);
constexpr auto kLogInterval = std::chrono::seconds(2);
constexpr float kToastSeconds = 4.0f;
constexpr uint32_t kSeqRestartGap = 600;  // a sequence this far behind means the peer restarted, not a late packet

struct Heard {
    player_sync::PlayerState state;
    Clock::time_point at;
    float velocity[3] = {};  // metres per second between the two newest reports
};

std::mutex g_mutex;  // guards g_heard and g_names (net thread writes, render thread reads)
std::map<uint8_t, Heard> g_heard;
std::map<uint8_t, std::string> g_names;

Clock::time_point g_lastSend;
Clock::time_point g_lastLog;
uint32_t g_seq = 0;

// Toasts the partner's death on the report that first says dead (caller holds g_mutex).
void toastDeath(uint8_t slot, const Heard* last, const Heard& now) {
    const bool wasDead = last && partner_status::decode(last->state.status).dead;
    if (wasDead || !partner_status::decode(now.state.status).dead) return;
    const auto name = g_names.find(slot);
    toast_queue::push(((name == g_names.end() ? "Player " + std::to_string(slot) : name->second) + " died").c_str(),
                      kToastSeconds);
}

void onFrame(const GameFrame& frame) {
    if (frame.type != player_sync::kMsgPlayerState || frame.payload.size() != sizeof(player_sync::PlayerState)) return;
    Heard heard{{}, Clock::now()};
    std::memcpy(&heard.state, frame.payload.data(), sizeof(heard.state));
    debug_stats::count(debug_stats::Counter::PlayerStateReceived);
    std::lock_guard lock(g_mutex);
    const auto previous = g_heard.find(frame.slot);
    if (previous != g_heard.end()) {
        const Heard& last = previous->second;
        const bool restarted = heard.state.seq < last.state.seq && last.state.seq - heard.state.seq > kSeqRestartGap;
        if (heard.state.seq <= last.state.seq && !restarted) return;  // late or duplicate datagram
        const float gapSeconds =
            restarted ? 0.0f : static_cast<float>(heard.state.seq - last.state.seq) / player_sync::kSendHz;
        position_blend::velocity(last.state.pos, heard.state.pos, gapSeconds, heard.velocity);
    }
    toastDeath(frame.slot, previous != g_heard.end() ? &previous->second : nullptr, heard);
    g_heard[frame.slot] = heard;
}

// Keeps the slot -> name table and toasts peers that joined or left since the previous tick.
void rememberNames(const SessionSnapshot& session) {
    std::map<uint8_t, std::string> now;
    for (const PeerInfo& peer : session.peers) now[peer.slot] = peer.name;
    std::lock_guard lock(g_mutex);
    for (const auto& [slot, name] : now) {
        if (!g_names.contains(slot)) toast_queue::push(("Co-op player joined: " + name).c_str(), kToastSeconds);
    }
    for (const auto& [slot, name] : g_names) {
        if (!now.contains(slot)) toast_queue::push(("Co-op player left: " + name).c_str(), kToastSeconds);
    }
    g_names = std::move(now);
    std::erase_if(g_heard, [](const auto& entry) { return !g_names.contains(entry.first); });
}

void sendLocal(NetClient& net) {
    const auto pose = game::localPlayer();
    if (!pose) return;
    player_sync::PlayerState state{};
    state.seq = ++g_seq;
    state.pos[0] = static_cast<float>(pose->position.x);
    state.pos[1] = static_cast<float>(pose->position.y);
    state.pos[2] = static_cast<float>(pose->position.z);
    state.yaw = pose->yaw;
    state.status = partner_status::encode(game::localStatus());
    if (!net.send(player_sync::kMsgPlayerState, false, proto::kSlotAll, proto::bytesOf(state))) return;
    debug_stats::count(debug_stats::Counter::PlayerStateSent);
    const auto now = Clock::now();
    if (now - g_lastLog < kLogInterval) return;
    g_lastLog = now;
    logger::write("local player seq=%u pos=(%.2f, %.2f, %.2f) yaw=%.2f", state.seq, state.pos[0], state.pos[1],
                  state.pos[2], state.yaw);
}

void tick(NetClient& net) {
    const SessionSnapshot session = net.poll([&net](const GameFrame& frame) {
        onFrame(frame);
        cargo_transfer::onFrame(net, frame);
        cargo_ground::onFrame(net, frame);
        vehicle_sync::onFrame(frame);
        vehicle_load::onFrame(frame);
        fact_sync::onFrame(frame);
        env_sync::onFrame(frame);
        bt_sync::onFrame(frame);
        struct_sync::onFrame(frame);
        story_sync::onFrame(frame);
        enemy_sync::onFrame(frame);
        camp_sync::onFrame(frame);
        enemy_combat::onFrame(frame);
        partner_cargo_sync::onFrame(frame);
        equip_sync::onFrame(frame);
        weapon_sync::onFrame(frame);
        anim_sync::onFrame(frame);
        anim_event::onFrame(frame);
        clock_sync::onFrame(net, frame);
        authority_sync::onFrame(frame);
        resync::onFrame(frame);
    });
    debug_stats::setSession(session);
    rememberNames(session);
    const bool guest = session.linked && session.localSlot != session.hostSlot;
    game::blockScriptedInteractions(guest);
    game::blockOrders(guest);
    game::tameEnemies(guest);
    cargo_transfer::tick(net, session);
    cargo_ground::tick(net, session);
    vehicle_sync::tick(net, session);
    vehicle_load::tick(net, session);
    fact_sync::tick(net, session);
    env_sync::tick(net, session);
    bt_sync::tick(net, session);
    struct_sync::tick(net, session);
    story_sync::tick(net, session);
    enemy_sync::tick(net, session);
    camp_sync::tick(net, session);
    enemy_combat::tick(net, session);
    partner_cargo_sync::tick(net, session);
    equip_sync::tick(net, session);
    weapon_sync::tick(net, session);
    rack_sync::tick(net, session);
    anim_sync::tick(net, session);
    clock_sync::tick(net, session);
    authority_sync::tick(net, session);
    resync_trigger::poll(net);
    const std::string rejects = reject_counters::summaryIfDue(nowUs());
    if (!rejects.empty()) logger::write("%s", rejects.c_str());
    const auto now = Clock::now();
    if (now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    sendLocal(net);
}

}  // namespace

namespace player_sync {

void start(NetClient& net, uint16_t port) {
    net.start(port, kGameId, [&net] { tick(net); });
}

std::vector<RemotePlayer> remotePlayers() {
    const auto now = Clock::now();
    std::vector<RemotePlayer> out;
    std::lock_guard lock(g_mutex);
    for (const auto& [slot, heard] : g_heard) {
        if (now - heard.at > kStaleAfter) continue;
        const auto name = g_names.find(slot);
        RemotePlayer peer{slot, name == g_names.end() ? "Player " + std::to_string(slot) : name->second, {},
                          {heard.velocity[0], heard.velocity[1], heard.velocity[2]}, heard.state.yaw,
                          partner_status::decode(heard.state.status)};
        const float elapsed = std::chrono::duration<float>(now - heard.at).count();
        position_blend::extrapolate(heard.state.pos, heard.velocity, elapsed, peer.position);
        out.push_back(std::move(peer));
    }
    return out;
}

}  // namespace player_sync
