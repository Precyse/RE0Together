#include "vehicle_load.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

#include "game.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kCheckInterval = std::chrono::milliseconds(500);
constexpr auto kReportInterval = std::chrono::seconds(5);  // re-sent this often even unchanged

// Net thread only.
Clock::time_point g_lastCheck;
Clock::time_point g_lastReport;
uint64_t g_reportedVehicle = 0;
std::vector<uint32_t> g_reportedKinds;

std::vector<uint32_t> sortedKinds(const std::vector<game::Cargo>& pieces) {
    std::vector<uint32_t> kinds;
    for (const game::Cargo& piece : pieces) kinds.push_back(piece.type);
    std::sort(kinds.begin(), kinds.end());
    return kinds;
}

void report(NetClient& net, Clock::time_point now) {
    const auto vehicle = game::drivenVehicle();
    if (!vehicle) {
        g_reportedVehicle = 0;
        return;
    }
    std::vector<uint32_t> kinds = sortedKinds(game::vehicleCargo(vehicle->id));
    kinds.resize(std::min<size_t>(kinds.size(), vehicle_load::kMaxPieces));
    const bool changed = vehicle->id != g_reportedVehicle || kinds != g_reportedKinds;
    if (!changed && now - g_lastReport < kReportInterval) return;
    const vehicle_load::VehicleLoad header{vehicle->id, static_cast<uint32_t>(kinds.size()), 0};
    std::vector<uint8_t> payload(sizeof(header) + kinds.size() * sizeof(uint32_t));
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!kinds.empty()) std::memcpy(payload.data() + sizeof(header), kinds.data(), kinds.size() * sizeof(uint32_t));
    if (!net.send(vehicle_load::kMsgVehicleLoad, true, proto::kSlotAll, payload)) return;
    g_reportedVehicle = vehicle->id;
    g_reportedKinds = std::move(kinds);
    g_lastReport = now;
}

// Brings this world's copy of the vehicle's bed to the reported kinds.
void follow(uint64_t vehicle, const std::vector<uint32_t>& wanted) {
    const auto own = game::drivenVehicle();
    if (own && own->id == vehicle) return;  // the local driver owns this one
    std::map<uint32_t, std::vector<uint64_t>> have;
    for (const game::Cargo& piece : game::vehicleCargo(vehicle)) have[piece.type].push_back(piece.handle);
    std::map<uint32_t, size_t> want;
    for (uint32_t kind : wanted) ++want[kind];
    int removed = 0, added = 0;
    for (const auto& [kind, handles] : have) {
        for (size_t i = want[kind]; i < handles.size(); ++i) removed += game::removeCargo(handles[i]);
    }
    for (const auto& [kind, count] : want) {
        const size_t present = have.contains(kind) ? have[kind].size() : 0;
        for (size_t i = present; i < count; ++i) added += game::addVehicleCargo(vehicle, kind);
    }
    if (removed || added) {
        logger::write("vehicle_load: vehicle %llx: %d pieces removed, %d added",
                      static_cast<unsigned long long>(vehicle), removed, added);
    }
}

}  // namespace

namespace vehicle_load {

void onFrame(const GameFrame& frame) {
    VehicleLoad header;
    if (frame.type != kMsgVehicleLoad || frame.payload.size() < sizeof(header)) return;
    std::memcpy(&header, frame.payload.data(), sizeof(header));
    if (header.count > kMaxPieces || frame.payload.size() != sizeof(header) + header.count * sizeof(uint32_t)) return;
    std::vector<uint32_t> kinds(header.count);
    if (header.count) std::memcpy(kinds.data(), frame.payload.data() + sizeof(header), header.count * sizeof(uint32_t));
    follow(header.vehicle, kinds);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    if (!session.linked || now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    report(net, now);
}

}  // namespace vehicle_load
