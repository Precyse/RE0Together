#include "ds2/remote_ride.h"


#include <chrono>
#include <cstdint>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "hooks.h"
#include "ds2/place.h"
#include "ds2/player_state.h"
#include "ds2/remote_player.h"
#include "ds2/setdriver_guard.h"
#include "ds2/vehicle.h"
#include "game.h"
#include "log.h"
#include "vehicle_sync.h"

namespace {

using Clock = std::chrono::steady_clock;

// The ride plugin sits on the remote's DSPlayerState (ds2::ridePlugin).
constexpr uintptr_t kPluginPhase = 0x118;
constexpr uintptr_t kPluginRequestedPhase = 0x11a;
constexpr uintptr_t kPluginOwner = 0x38;
// The owner's ride request: the engine's own TryStart accepts a request kind of 3 or 9 with a vehicle's entity id.
constexpr uintptr_t kOwnerRequestKind = 0x3970;
constexpr uintptr_t kOwnerRequestTarget = 0x39e0;
constexpr uint8_t kRequestEnter = 3;
constexpr uint8_t kPhaseOnFoot = 0;
constexpr uint8_t kPhaseRideOn = 1;
constexpr uint8_t kPhaseDrive = 2;
constexpr uint8_t kPhaseRideOff = 3;

constexpr uintptr_t kClearParent = 0x14014ae00;
constexpr uintptr_t kMoverUpdate = 0x140ec81e0;  // DSPlayerMover update (message handler)
constexpr uintptr_t kComponentOwner = 0x48;

// The driver's door, in the vehicle's frame (right, forward, up): where the engine's door check lets a player board.
constexpr double kDoorRight = 2.6;
constexpr double kDoorForward = 1.8;
constexpr double kDoorUp = 0.8;
constexpr auto kBoardRetry = std::chrono::seconds(3);
constexpr double kBoardReachMetres = 6.0;  // from Sam to the vehicle's origin
// The passenger seat, in the vehicle's frame: the other of the two seat pods on the cab roof (the driver's is at
// right 0.40, forward 1.57, up 2.36).
constexpr double kPassengerRight = -0.40;
constexpr double kPassengerForward = 1.57;
constexpr double kPassengerUp = 2.36;

enum class Stage { OnFoot, Boarding, Riding, Leaving };

Stage g_stage = Stage::OnFoot;
Clock::time_point g_boardedAt;
Clock::time_point g_samBoardedAt;  // the last update that seated the local player
bool g_passenger = false;     // the peer reports a passenger seat: the remote sits in the second pod
bool g_followsLocal = false;  // the local player drives the same vehicle: the remote rides only while he does
uint64_t g_vehicleId = 0;
uint64_t g_localPassengerSeen = 0;  // the vehicle the local player was last seated in as a passenger

using ClearParentFn = void (*)(uintptr_t entity);

// The remote beside the vehicle's driver door, facing it.
void standAtDriverDoor(uintptr_t vehicle) {
    decima::WorldTransform truck{};
    if (!decima::safeRead(vehicle + ds2::kEntityTransform, truck)) return;
    const auto& r = truck.orientation.row;
    decima::WorldTransform door = truck;
    door.position.x += r[0][0] * kDoorRight + r[1][0] * kDoorForward + r[2][0] * kDoorUp;
    door.position.y += r[0][1] * kDoorRight + r[1][1] * kDoorForward + r[2][1] * kDoorUp;
    door.position.z += r[0][2] * kDoorRight + r[1][2] * kDoorForward + r[2][2] * kDoorUp;
    for (int i = 0; i < 3; ++i) {  // facing the vehicle: forward is the vehicle's left
        door.orientation.row[0][i] = r[1][i];
        door.orientation.row[1][i] = -r[0][i];
        door.orientation.row[2][i] = r[2][i];
    }
    ds2::teleportEntity(remote_player::entity(), door);
}

void requestBoarding(uintptr_t plugin, uintptr_t vehicle) {
    standAtDriverDoor(vehicle);
    const uintptr_t owner = decima::readPointer(plugin + kPluginOwner);
    if (!owner) return;
    ds2::field<uint64_t>(owner, kOwnerRequestTarget) = ds2::field<uint64_t>(vehicle, ds2::kEntityNetworkId);
    ds2::field<uint8_t>(owner, kOwnerRequestKind) = kRequestEnter;
    g_boardedAt = Clock::now();
}

// A request left set would put the remote straight back in after it left.
void clearRequest(uintptr_t plugin) {
    const uintptr_t owner = decima::readPointer(plugin + kPluginOwner);
    if (owner) ds2::field<uint8_t>(owner, kOwnerRequestKind) = 0;
}

// The pod's world transform on the vehicle, or false when the vehicle is not readable.
bool passengerSeat(uintptr_t vehicle, decima::WorldTransform& seat) {
    decima::WorldTransform truck{};
    if (!decima::safeRead(vehicle + ds2::kEntityTransform, truck)) return false;
    const auto& r = truck.orientation.row;
    seat = truck;
    seat.position.x += r[0][0] * kPassengerRight + r[1][0] * kPassengerForward + r[2][0] * kPassengerUp;
    seat.position.y += r[0][1] * kPassengerRight + r[1][1] * kPassengerForward + r[2][1] * kPassengerUp;
    seat.position.z += r[0][2] * kPassengerRight + r[1][2] * kPassengerForward + r[2][2] * kPassengerUp;
    return true;
}

// The local player as a passenger of the vehicle the partner drives (setdriver_guard answers his SetDriver): he is moved
// to the second pod right after his mover wrote the driver's seat, and let go when his ride states have ended.
void seatLocalPassenger() {
    const uint64_t vehicleId = setdriver_guard::localPassengerVehicle();
    if (vehicleId != g_localPassengerSeen) {
        g_localPassengerSeen = vehicleId;
        g_samBoardedAt = Clock::now();  // the ride states take a moment to leave the on-foot phase
    }
    if (!vehicleId) return;
    const uintptr_t plugin = ds2::ridePlugin(remote_player::samEntity());
    decima::WorldTransform seat{};
    if (plugin && ds2::field<uint8_t>(plugin, kPluginPhase) == kPhaseOnFoot) {
        if (Clock::now() - g_samBoardedAt > kBoardRetry) setdriver_guard::endLocalPassenger();
        return;
    }
    if (passengerSeat(ds2::loadedVehicle(vehicleId), seat)) ds2::teleportEntity(remote_player::samEntity(), seat);
    g_samBoardedAt = Clock::now();
}

// DSPlayerMover's update writes a seated player's transform (the driver's seat) into the entity itself. The remote,
// as a passenger, is moved to the second pod right after that call, on the same thread and inside the same update.
using MoverUpdateFn = uint64_t (*)(uintptr_t mover, uintptr_t message, uintptr_t a3, uintptr_t a4, uintptr_t a5,
                                   uintptr_t a6, uintptr_t a7, uintptr_t a8);
MoverUpdateFn g_moverUpdate = nullptr;

uint64_t moverUpdateDetour(uintptr_t mover, uintptr_t message, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,
                           uintptr_t a7, uintptr_t a8) {
    const uint64_t result = g_moverUpdate(mover, message, a3, a4, a5, a6, a7, a8);
    const uintptr_t remote = remote_player::entity();
    decima::WorldTransform seat{};
    if (ds2::field<uintptr_t>(mover, kComponentOwner) == remote_player::samEntity()) seatLocalPassenger();
    if (remote && g_stage == Stage::Riding && g_passenger && ds2::field<uintptr_t>(mover, kComponentOwner) == remote &&
        passengerSeat(ds2::loadedVehicle(g_vehicleId), seat)) {
        ds2::teleportEntity(remote, seat);
    }
    return result;
}

// Whether the local player drives the vehicle too (each machine keeps its own driver; the partner then rides along).
bool localDrives(uint64_t vehicleId) {
    const auto own = game::drivenVehicle();
    return own && own->id == vehicleId;
}

void releaseVehicle() {
    reinterpret_cast<ClearParentFn>(ds2::at(kClearParent))(remote_player::entity());  // the engine leaves it linked
    g_stage = Stage::OnFoot;
}

}  // namespace

namespace remote_ride {

void reset() {
    g_stage = Stage::OnFoot;
    g_passenger = false;
    g_followsLocal = false;
    g_vehicleId = 0;
    g_localPassengerSeen = 0;
}

bool holdsBody() { return g_stage != Stage::OnFoot; }

void installEarly() {
    hooks::install("player mover update", ds2::at(kMoverUpdate), reinterpret_cast<void*>(&moverUpdateDetour),
                   reinterpret_cast<void**>(&g_moverUpdate));
}

// The local player on foot beside the vehicle the partner drives presses F: the game's own prompt is open for him (see
// setdriver_guard) but its action does not start a ride, so the same request the remote uses is written to his ride
// plugin. Boards as a passenger; leaving is the game's own (F again).
void boardLocalPassenger() {
    static bool wasDown = false;
    const bool down = (GetAsyncKeyState('F') & 0x8000) != 0;
    const bool edge = down && !wasDown;
    wasDown = down;
    if (!edge || setdriver_guard::localPassengerVehicle() || game::drivenVehicle()) return;
    const auto riding = vehicle_sync::partnerRiding(remote_player::slot());
    const uintptr_t vehicle = riding && riding->role == vehicle_sync::kRoleDriver ? ds2::loadedVehicle(riding->id) : 0;
    const uintptr_t plugin = ds2::ridePlugin(remote_player::samEntity());
    const uintptr_t owner = plugin ? decima::readPointer(plugin + kPluginOwner) : 0;
    decima::WorldTransform truck{}, sam{};
    if (!vehicle || !owner || !decima::safeRead(vehicle + ds2::kEntityTransform, truck) ||
        !decima::safeRead(remote_player::samEntity() + ds2::kEntityTransform, sam)) {
        return;
    }
    const double dx = truck.position.x - sam.position.x, dy = truck.position.y - sam.position.y,
                 dz = truck.position.z - sam.position.z;
    if (dx * dx + dy * dy + dz * dz > kBoardReachMetres * kBoardReachMetres) return;
    ds2::field<uint64_t>(owner, kOwnerRequestTarget) = ds2::field<uint64_t>(vehicle, ds2::kEntityNetworkId);
    ds2::field<uint8_t>(owner, kOwnerRequestKind) = kRequestEnter;
    logger::write("remote_ride: the local player boards the partner's vehicle as a passenger");
}

void tick() {
    boardLocalPassenger();
    const uintptr_t plugin = ds2::ridePlugin(remote_player::entity());
    if (!plugin) return;
    const uint8_t phase = ds2::field<uint8_t>(plugin, kPluginPhase);
    const auto riding = vehicle_sync::partnerRiding(remote_player::slot());
    const auto driven = riding ? std::optional<uint64_t>(riding->id) : std::nullopt;
    switch (g_stage) {
        case Stage::OnFoot:
            if (const uintptr_t vehicle = driven ? ds2::loadedVehicle(*driven) : 0) {
                g_vehicleId = *driven;
                g_followsLocal = localDrives(g_vehicleId);
                g_passenger = g_followsLocal || riding->role == vehicle_sync::kRolePassenger;
                requestBoarding(plugin, vehicle);
                g_stage = Stage::Boarding;
                logger::write("remote_ride: boarding vehicle %llx as %s", static_cast<unsigned long long>(g_vehicleId),
                              g_passenger ? "a passenger" : "the driver");
            }
            break;
        case Stage::Boarding:
            if (phase == kPhaseRideOn || phase == kPhaseDrive) clearRequest(plugin);
            if (phase == kPhaseDrive) {
                g_stage = Stage::Riding;
                logger::write("remote_ride: seated");
            } else if (!driven) {  // the partner left before the remote sat down
                clearRequest(plugin);
                if (phase == kPhaseOnFoot) g_stage = Stage::OnFoot;
            } else if (phase == kPhaseOnFoot && Clock::now() - g_boardedAt > kBoardRetry) {
                if (const uintptr_t vehicle = ds2::loadedVehicle(*driven)) requestBoarding(plugin, vehicle);
            }
            break;
        case Stage::Riding:
            if (phase == kPhaseOnFoot) {  // the engine ended the ride itself
                releaseVehicle();
            } else if (!driven || (g_followsLocal && !localDrives(g_vehicleId))) {
                ds2::field<uint8_t>(plugin, kPluginRequestedPhase) = kPhaseRideOff;
                g_stage = Stage::Leaving;
                logger::write("remote_ride: leaving");
            }
            break;
        case Stage::Leaving:
            if (phase == kPhaseOnFoot) {
                releaseVehicle();
                logger::write("remote_ride: on foot");
            }
            break;
    }
}

}  // namespace remote_ride
