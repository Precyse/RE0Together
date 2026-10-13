#include "vehicle_sync.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "authority_sync.h"
#include "ds2/setdriver_guard.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"
#include "position_blend.h"
#include "vehicle_wire.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kSendInterval = std::chrono::microseconds(static_cast<int>(1'000'000 / vehicle_wire::kSendHz));
constexpr auto kStaleAfter = std::chrono::milliseconds(500);  // no report this long: the partner left the vehicle
constexpr uint32_t kSeqRestartGap = 300;  // a sequence this far behind means the sender restarted

struct Driven {
    vehicle_wire::VehicleState state;
    Clock::time_point at;
    float velocity[3] = {};
    bool failed = false;  // the vehicle is not loaded here, or moving it faulted (logged once)
};

std::mutex g_mutex;  // guards g_driven (net thread writes, simulation thread places)
std::map<uint8_t, Driven> g_driven;  // per partner slot: the vehicle it drives

// Whether a partner reports driving this vehicle right now. Caller holds no lock.
bool partnerDrives(uint64_t id) {
    std::lock_guard lock(g_mutex);
    const auto now = Clock::now();
    for (const auto& [slot, driven] : g_driven) {
        if (driven.state.id == id && driven.state.role == vehicle_wire::kRoleDriver && now - driven.at <= kStaleAfter) {
            return true;
        }
    }
    return false;
}

// Net thread only.
Clock::time_point g_lastSend;
uint32_t g_seq = 0;
uint64_t g_lastDriven = 0;
std::atomic<uint32_t> g_localRole{vehicle_wire::kRoleDriver};  // of the vehicle the local player is in

// The vehicle the local player is in: the one he drives, else the one he rides as a passenger of the partner's.
std::optional<game::VehiclePose> localVehicle() {
    if (const auto driven = game::drivenVehicle()) return driven;
    const uint64_t seatedIn = setdriver_guard::localPassengerVehicle();
    return seatedIn ? game::vehiclePose(seatedIn) : std::nullopt;
}

// The first to claim the vehicle drives it; the one that gets in second rides along. A passenger of the partner's vehicle
// (setdriver_guard) never claims it.
uint32_t roleOnEntering(uint64_t id, bool drivesIt) {
    if (!drivesIt) return vehicle_wire::kRolePassenger;
    const bool claimed = authority_sync::claim(id);
    return claimed && !partnerDrives(id) ? vehicle_wire::kRoleDriver : vehicle_wire::kRolePassenger;
}

void sendLocal(NetClient& net) {
    const auto vehicle = localVehicle();
    if (!vehicle) {
        if (g_lastDriven) {
            logger::write("vehicle_sync: left vehicle %llx", static_cast<unsigned long long>(g_lastDriven));
            authority_sync::release(g_lastDriven);
        }
        g_lastDriven = 0;
        return;
    }
    if (vehicle->id != g_lastDriven) {
        if (g_lastDriven) authority_sync::release(g_lastDriven);
        g_localRole = roleOnEntering(vehicle->id, game::drivenVehicle().has_value());
        logger::write("vehicle_sync: %s vehicle %llx", g_localRole == vehicle_wire::kRolePassenger ? "riding" : "driving",
                      static_cast<unsigned long long>(vehicle->id));
    }
    g_lastDriven = vehicle->id;
    vehicle_wire::VehicleState state{++g_seq, g_localRole.load(), vehicle->id, {static_cast<float>(vehicle->position.x),
                                     static_cast<float>(vehicle->position.y), static_cast<float>(vehicle->position.z)},
                                     {}};
    std::memcpy(state.rotation, vehicle->rotation, sizeof(state.rotation));
    net.send(vehicle_wire::kMsgVehicleState, false, proto::kSlotAll, proto::bytesOf(state));
}

}  // namespace

namespace vehicle_sync {

void onFrame(const GameFrame& frame) {
    Driven heard{{}, Clock::now()};
    if (frame.type != vehicle_wire::kMsgVehicleState || !vehicle_wire::decode(frame.payload, heard.state)) return;
    std::lock_guard lock(g_mutex);
    const auto previous = g_driven.find(frame.slot);
    if (previous != g_driven.end() && previous->second.state.id == heard.state.id) {
        const Driven& last = previous->second;
        const bool restarted = heard.state.seq < last.state.seq && last.state.seq - heard.state.seq > kSeqRestartGap;
        if (heard.state.seq <= last.state.seq && !restarted) return;  // late or duplicate datagram
        const float gap = restarted ? 0.0f : static_cast<float>(heard.state.seq - last.state.seq) / vehicle_wire::kSendHz;
        position_blend::velocity(last.state.position, heard.state.position, gap, heard.velocity);
        heard.failed = last.failed;
    } else {
        logger::write("vehicle_sync: slot %u %s vehicle %llx", frame.slot,
                      heard.state.role == vehicle_wire::kRolePassenger ? "rides" : "drives", static_cast<unsigned long long>(heard.state.id));
    }
    g_driven[frame.slot] = heard;
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    if (!session.linked || now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    sendLocal(net);
}

void place() {
    const auto now = Clock::now();
    const auto own = game::drivenVehicle();  // the local driver keeps its vehicle if both claim one
    std::lock_guard lock(g_mutex);
    for (auto it = g_driven.begin(); it != g_driven.end();) {
        Driven& driven = it->second;
        if (now - driven.at > kStaleAfter) {
            logger::write("vehicle_sync: slot %u stopped driving", it->first);
            it = g_driven.erase(it);
            continue;
        }
        const bool localKeeps = own && own->id == driven.state.id && g_localRole == vehicle_wire::kRoleDriver;
        if (!driven.failed && driven.state.role == vehicle_wire::kRoleDriver && !localKeeps) {
            game::VehiclePose pose{driven.state.id, {}, {}};
            float at[3];
            position_blend::extrapolate(driven.state.position, driven.velocity,
                                        std::chrono::duration<float>(now - driven.at).count(), at);
            pose.position = {at[0], at[1], at[2]};
            std::memcpy(pose.rotation, driven.state.rotation, sizeof(pose.rotation));
            const world_to_screen::Vec3 velocity{driven.velocity[0], driven.velocity[1], driven.velocity[2]};
            if (!game::placeVehicle(pose, velocity)) {
                driven.failed = true;
                logger::write("vehicle_sync: vehicle %llx not loaded here or not movable",
                              static_cast<unsigned long long>(driven.state.id));
            }
        }
        ++it;
    }
}

std::optional<Riding> partnerRiding(uint8_t slot) {
    std::lock_guard lock(g_mutex);
    const auto driven = g_driven.find(slot);
    if (driven == g_driven.end() || Clock::now() - driven->second.at > kStaleAfter) return std::nullopt;
    return Riding{driven->second.state.id, driven->second.state.role};
}

void installEarly() { sim_tick::add(&place, "vehicle placement", sim_tick::Gate::Gameplay); }

}  // namespace vehicle_sync
