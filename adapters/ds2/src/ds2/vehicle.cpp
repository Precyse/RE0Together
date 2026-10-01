// DEATH STRANDING 2: vehicles through DSVehicleManager. The manager keeps one DSVehicleContentInfo record per vehicle
// of the world (trucks and bikes, 16 in the test save), 0x110 bytes apart from +0x30: +0x8 the vehicle's id (saved with
// the world, also its baggage owner key), +0x88 its entity while loaded. The vehicle the player drives is the entity at
// +0x79E0 +0xF8, as the script export MovePlayerDrivingVehicle reads it (docs/DS2_NOTES.md, "Vehicles").
#include <cstring>
#include <optional>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/place.h"
#include "game.h"
#include "log.h"
#include "pattern_scan.h"

namespace {

// VehicleEntity_sExportedMovePlayerDrivingVehicle (0x141f6d050): `mov rax, [DSVehicleManager]` at +16.
constexpr const char* kMovePlayerDrivingVehicle =
    "48 89 6C 24 18 48 89 74 24 20 41 56 48 83 EC 30 48 8B 05 ?? ?? ?? ?? 45 33 F6 48 8B E9 48 85 C0 74 08 48 05 E0 "
    "79 00 00";
constexpr int kManagerDisp = 19, kManagerEnd = 23;

constexpr uintptr_t kRecords = 0x30;  // the first DSVehicleContentInfo
constexpr size_t kRecordStride = 0x110;
constexpr int kMaxRecords = 256;
constexpr uintptr_t kRecordId = 0x8, kRecordEntity = 0x88;
constexpr uintptr_t kDriven = 0x79E0 + 0xF8;  // the entity the local player drives, 0 on foot

uintptr_t managerGlobal() {
    static const uintptr_t found = [] {
        const uintptr_t code = pattern_scan::find(kMovePlayerDrivingVehicle);
        const uintptr_t global = code ? pattern_scan::ripTarget(code, kManagerDisp, kManagerEnd) : 0;
        logger::write("vehicle: manager %p", reinterpret_cast<void*>(global));
        return global;
    }();
    return found;
}

uintptr_t manager() { return managerGlobal() ? decima::readPointer(managerGlobal()) : 0; }

// Walks the records while they carry the first record's vtable; `visit` returns true to stop.
template <class Visit>
void forEachRecord(uintptr_t vehicles, Visit visit) {
    const uintptr_t vtable = decima::readPointer(vehicles + kRecords);
    for (int i = 0; vtable && i < kMaxRecords; ++i) {
        const uintptr_t record = vehicles + kRecords + i * kRecordStride;
        if (decima::readPointer(record) != vtable) return;
        uint64_t id = 0;
        if (decima::safeRead(record + kRecordId, id) && visit(id, decima::readPointer(record + kRecordEntity))) return;
    }
}

}  // namespace

namespace game {

std::optional<VehiclePose> drivenVehicle() {
    const uintptr_t vehicles = manager();
    const uintptr_t driven = vehicles ? decima::readPointer(vehicles + kDriven) : 0;
    decima::WorldTransform t;
    if (!driven || !ds2::entityTransform(driven, t)) return std::nullopt;
    std::optional<VehiclePose> out;
    forEachRecord(vehicles, [&](uint64_t id, uintptr_t entity) {
        if (entity != driven) return false;
        VehiclePose pose{id, {t.position.x, t.position.y, t.position.z}, {}};
        std::memcpy(pose.rotation, t.orientation.row, sizeof(pose.rotation));
        out = pose;
        return true;
    });
    return out;
}

bool placeVehicle(const VehiclePose& pose, const world_to_screen::Vec3& velocity) {
    const uintptr_t vehicles = manager();
    uintptr_t target = 0;
    if (vehicles) {
        forEachRecord(vehicles, [&](uint64_t id, uintptr_t entity) {
            if (id == pose.id) target = entity;
            return id == pose.id;
        });
    }
    if (!target) return false;
    decima::WorldTransform t{};
    t.position = {pose.position.x, pose.position.y, pose.position.z};
    std::memcpy(t.orientation.row, pose.rotation, sizeof(pose.rotation));
    return ds2::placeEntity(target, t, velocity);
}

}  // namespace game
