#include "env_sync.h"

#include <chrono>
#include <cstring>

#include "env_wire.h"
#include "game.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kSendInterval = std::chrono::seconds(1);  // the host repeats the state this often
constexpr auto kCheckInterval = std::chrono::milliseconds(200);
constexpr float kTimeJumpHours = 0.05f;  // a change of the clock beyond what one second of play brings

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
Clock::time_point g_lastCheck;
Clock::time_point g_lastSent;
env_wire::WorldEnv g_sent{};
bool g_sentOnce = false;

bool changedSince(const env_wire::WorldEnv& env) {
    if (!g_sentOnce) return true;
    return std::memcmp(env.regionType, g_sent.regionType, sizeof(env.regionType)) != 0 ||
           env_wire::hoursApart(env.timeOfDay, g_sent.timeOfDay) > kTimeJumpHours || env.day != g_sent.day;
}

}  // namespace

namespace env_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != env_wire::kMsgWorldEnv || !g_guest || frame.slot != g_hostSlot) return;
    env_wire::WorldEnv env;
    if (!env_wire::decode(frame.payload, env)) {
        logger::write("env_sync: dropped a malformed WORLD_ENV (%zu bytes)", frame.payload.size());
        return;
    }
    game::followWorldEnv(env);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    const bool guest = session.linked && !host;
    game::keepWorldClockRunning(session.linked);
    game::keepWorldRunning(session.linked);
    if (g_guest && !guest) game::releaseWorldEnv();
    g_guest = guest;
    g_hostSlot = session.hostSlot;
    const auto now = Clock::now();
    if (!host || now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    env_wire::WorldEnv env;
    if (!game::readWorldEnv(env)) return;
    if (!changedSince(env) && now - g_lastSent < kSendInterval) return;
    if (!net.send(env_wire::kMsgWorldEnv, true, proto::kSlotAll, proto::bytesOf(env))) return;
    g_sent = env;
    g_sentOnce = true;
    g_lastSent = now;
}

}  // namespace env_sync
