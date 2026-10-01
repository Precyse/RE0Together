#include "player_sync.h"

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "debug_stats.h"
#include "game.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kGameId = "ds2";
constexpr auto kSendInterval = std::chrono::microseconds(static_cast<int>(1'000'000 / player_sync::kSendHz));
constexpr auto kStaleAfter = std::chrono::seconds(3);
constexpr auto kLogInterval = std::chrono::seconds(2);

struct Heard {
    player_sync::PlayerState state;
    Clock::time_point at;
};

std::mutex g_mutex;  // guards g_heard and g_names (net thread writes, render thread reads)
std::map<uint8_t, Heard> g_heard;
std::map<uint8_t, std::string> g_names;

Clock::time_point g_lastSend;
Clock::time_point g_lastLog;
uint32_t g_seq = 0;

void onFrame(const GameFrame& frame) {
    if (frame.type != player_sync::kMsgPlayerState || frame.payload.size() != sizeof(player_sync::PlayerState)) return;
    Heard heard{{}, Clock::now()};
    std::memcpy(&heard.state, frame.payload.data(), sizeof(heard.state));
    debug_stats::count(debug_stats::Counter::PlayerStateReceived);
    std::lock_guard lock(g_mutex);
    g_heard[frame.slot] = heard;
}

void rememberNames(const SessionSnapshot& session) {
    std::lock_guard lock(g_mutex);
    g_names.clear();
    for (const PeerInfo& peer : session.peers) g_names[peer.slot] = peer.name;
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
    if (!net.send(player_sync::kMsgPlayerState, false, proto::kSlotAll, proto::bytesOf(state))) return;
    debug_stats::count(debug_stats::Counter::PlayerStateSent);
    const auto now = Clock::now();
    if (now - g_lastLog < kLogInterval) return;
    g_lastLog = now;
    logger::write("local player seq=%u pos=(%.2f, %.2f, %.2f) yaw=%.2f", state.seq, state.pos[0], state.pos[1],
                  state.pos[2], state.yaw);
}

void tick(NetClient& net) {
    const SessionSnapshot session = net.poll(onFrame);
    debug_stats::setSession(session);
    rememberNames(session);
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
        out.push_back({slot, name == g_names.end() ? "Player " + std::to_string(slot) : name->second,
                       {heard.state.pos[0], heard.state.pos[1], heard.state.pos[2]}, heard.state.yaw});
    }
    return out;
}

}  // namespace player_sync
