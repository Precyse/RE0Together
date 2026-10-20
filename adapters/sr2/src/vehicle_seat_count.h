#pragma once
#include <cstdint>

#include "memory.h"
#include "sr2_addresses.h"
#include "vehicle_seating_rules.h"

namespace vehicle_seat_count {

// The seat count of a vehicle object, as the customisation exit reads it at 0x7B97BF..0x7B97D5: the declared count at +0x6CE8 when the
// byte at +0x84D8 is 1, else the default.
inline uint32_t of(uintptr_t vehicle) {
    return vehicle_seating_rules::seatCount(readByte(vehicle + sr2::kVehicleSeatsDeclaredOffset), readDword(vehicle + sr2::kVehicleSeatCountOffset));
}

}  // namespace vehicle_seat_count
