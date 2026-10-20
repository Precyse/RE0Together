#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

// The pure rules of vehicle seating for 3 or 4 players (docs/SR2_NOTES.md, "Vehicle seating"); no game reads.
namespace vehicle_seating_rules {

constexpr uint32_t kDefaultSeats = 8;  // a vehicle that does not declare its seat count
constexpr uint32_t kMaxSeats = 8;      // the seat array holds 8 seats
constexpr int32_t kFirstPassengerSeat = 1;
constexpr int32_t kDefaultHolder = -1;
constexpr std::string_view kSeatTracePrefix = "coop seat ";  // what the Lua seat rules start their mission_debug texts with

enum class Occupant : uint8_t { Empty, Npc, Player };

// The seat count of a vehicle: its declared count (never more than the array holds) when `declaredFlag` is 1, else the default.
constexpr uint32_t seatCount(uint8_t declaredFlag, uint32_t declaredCount) {
    return declaredFlag == 1 ? std::min(declaredCount, kMaxSeats) : kDefaultSeats;
}

// The seat a guest gets in a remote's car in Drug Trafficking: the scripted seat while no player sits there, else the first empty
// seat above it, else the first empty passenger seat below it, else the scripted seat (the car is full, as in the game). An NPC in
// the scripted seat keeps it, and the driver seat is never chosen.
constexpr int32_t chooseTraffickingSeat(std::span<const Occupant> seats, int32_t scripted) {
    const auto size = static_cast<int32_t>(seats.size());
    const auto at = [&](int32_t seat) { return seats[static_cast<size_t>(seat)]; };
    if (scripted < 0 || scripted >= size || at(scripted) != Occupant::Player) return scripted;
    for (int32_t seat = scripted + 1; seat < size; ++seat) {
        if (at(seat) == Occupant::Empty) return seat;
    }
    for (int32_t seat = kFirstPassengerSeat; seat < scripted; ++seat) {
        if (at(seat) == Occupant::Empty) return seat;
    }
    return scripted;
}

// True when `handle` is the vehicle a remote player is in. A handle of 0 is nobody's vehicle.
constexpr bool isRemoteVehicle(uint32_t handle, std::span<const uint32_t> remoteVehicleHandles) {
    return handle != 0 && std::find(remoteVehicleHandles.begin(), remoteVehicleHandles.end(), handle) != remoteVehicleHandles.end();
}

// True when a human whose vehicle handle is `humanVehicle` rides in the vehicle `vehicle`. A vehicle of 0 is nobody's vehicle, so a human on foot
// never matches it.
constexpr bool ridesVehicle(uint32_t humanVehicle, uint32_t vehicle) { return vehicle != 0 && humanVehicle == vehicle; }

// The remembered checkpoint holder while that session slot is still paired with a remote, else the default.
constexpr int32_t restoreHolderSlot(int32_t remembered, std::span<const uint8_t> pairedSlots) {
    const bool slotFits = remembered >= 0 && remembered <= UINT8_MAX;
    const bool paired = slotFits && std::find(pairedSlots.begin(), pairedSlots.end(), static_cast<uint8_t>(remembered)) != pairedSlots.end();
    return paired ? remembered : kDefaultHolder;
}

// True for a mission_debug text of the Lua seat rules: only those are logged, the game's own are many.
constexpr bool isSeatTrace(std::string_view text) { return text.starts_with(kSeatTracePrefix); }

}  // namespace vehicle_seating_rules
