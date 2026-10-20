// The pure rules of vehicle seating (no game): seat counts, the Trafficking seat of a guest, which cars are a remote's, the checkpoint
// holder, and which mission_debug texts are logged.
#include <array>
#include <cstdio>
#include <span>

#include "../src/vehicle_seating_rules.h"
#include "../src/vehicle_seating_state.h"
#include "check.h"

namespace {

using vehicle_seating_rules::Occupant;

constexpr int32_t kScriptedSeat = 2;
constexpr int32_t kDefault = vehicle_seating_rules::kDefaultHolder;
constexpr uint32_t kCarHandle = 0x1234;
constexpr uint8_t kDeclared = 1;

template <size_t N>
int32_t seatIn(const std::array<Occupant, N>& seats) {
    return vehicle_seating_rules::chooseTraffickingSeat(seats, kScriptedSeat);
}

void theSeatCount() {
    check(vehicle_seating_rules::seatCount(0, 4) == vehicle_seating_rules::kDefaultSeats, "an undeclared count is the default of 8");
    check(vehicle_seating_rules::seatCount(kDeclared, 4) == 4, "a declared count of 4 is 4");
    check(vehicle_seating_rules::seatCount(kDeclared, 0) == 0, "a declared count of 0 is 0 seats");
    check(vehicle_seating_rules::seatCount(kDeclared, 100) == vehicle_seating_rules::kMaxSeats, "a declared count above 8 is clamped to 8");
    check(vehicle_seating_rules::seatCount(2, 4) == vehicle_seating_rules::kDefaultSeats, "only a flag of 1 declares the count");
}

void theTraffickingSeat() {
    const Occupant e = Occupant::Empty;
    const Occupant n = Occupant::Npc;
    const Occupant p = Occupant::Player;
    check(seatIn(std::array{p, e, e, e}) == 2, "2 players, seat 2 empty: seat 2");
    check(seatIn(std::array{p, e, p, e}) == 3, "P2 in seat 2 of 4 seats: seat 3");
    check(seatIn(std::array{p, e, p, p}) == 1, "seats 2 and 3 taken by players, seat 1 empty: seat 1");
    check(seatIn(std::array{p, p, p, p}) == 2, "a full car: seat 2, as the game has it");
    check(seatIn(std::array{p, e, n, e}) == 2, "an NPC in seat 2 keeps seat 2");
    check(seatIn(std::array<Occupant, 0>{}) == 2, "a vehicle with 0 seats: seat 2");
    check(seatIn(std::array{p, e}) == 2, "a vehicle without seat 2: seat 2");
    check(seatIn(std::array{e, e, p, n, e}) == 4, "an NPC in seat 3 is not free, seat 4 is");
    check(seatIn(std::array{e, n, p, n}) == 2, "no empty seat but NPCs: seat 2");
    check(seatIn(std::array{e, e, p, p}) == 1, "the driver seat is never given");
    check(vehicle_seating_rules::chooseTraffickingSeat(std::array{Occupant::Player, Occupant::Empty}, -1) == -1, "a negative scripted seat stays");
}

void theRemoteVehicle() {
    const std::array<uint32_t, 2> cars{kCarHandle, 0x5678};
    check(vehicle_seating_rules::isRemoteVehicle(kCarHandle, cars), "a car a remote is in is a remote's car");
    check(!vehicle_seating_rules::isRemoteVehicle(0x9999, cars), "another car is not");
    check(!vehicle_seating_rules::isRemoteVehicle(0, std::array<uint32_t, 1>{0}), "handle 0 is never a remote vehicle");
    check(!vehicle_seating_rules::isRemoteVehicle(kCarHandle, std::span<const uint32_t>{}), "without remotes nothing is a remote's car");
}

void theCheckpointHolder() {
    const std::array<uint8_t, 2> paired{1, 2};
    check(vehicle_seating_rules::restoreHolderSlot(2, paired) == 2, "a remembered slot that is still paired is kept");
    check(vehicle_seating_rules::restoreHolderSlot(3, paired) == kDefault, "the remembered slot after a leave falls back to the default");
    check(vehicle_seating_rules::restoreHolderSlot(kDefault, paired) == kDefault, "nothing remembered is the default");
    check(vehicle_seating_rules::restoreHolderSlot(0, std::span<const uint8_t>{}) == kDefault, "no paired remote is the default");
    check(vehicle_seating_rules::restoreHolderSlot(256, std::array<uint8_t, 1>{0}) == kDefault, "a slot above a byte is never paired");
    check(vehicle_seating_state::checkpointHolder() == kDefault, "the state starts at the default");
    vehicle_seating_state::rememberCheckpointHolder(2);
    check(vehicle_seating_state::checkpointHolder() == 2, "the state keeps the slot MC4 recorded");
}

void theTraceFilter() {
    check(vehicle_seating_rules::isSeatTrace("coop seat evict P3 from v 1 for c"), "a Lua seat line is logged");
    check(!vehicle_seating_rules::isSeatTrace("hello"), "the game's own mission_debug text is not");
    check(!vehicle_seating_rules::isSeatTrace("coop seat"), "the prefix needs its trailing space");
    check(!vehicle_seating_rules::isSeatTrace(""), "an empty text is not");
}

}  // namespace

int main() {
    theSeatCount();
    theTraffickingSeat();
    theRemoteVehicle();
    theCheckpointHolder();
    theTraceFilter();
    return g_failures == 0 ? 0 : 1;
}
