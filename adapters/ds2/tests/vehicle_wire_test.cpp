// vehicle_wire: the VEHICLE_STATE payload checks, driver and passenger roles (no game).
#include <cstddef>
#include <cstdio>
#include <vector>

#include "../src/vehicle_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

vehicle_wire::VehicleState report(uint32_t role) {
    vehicle_wire::VehicleState state{};
    state.seq = 7;
    state.role = role;
    state.id = 0x0e001a000000001cULL;
    state.position[0] = 1234.5f;
    state.position[2] = 248.1f;
    state.rotation[0][0] = state.rotation[1][1] = state.rotation[2][2] = 1.0f;
    return state;
}

bool same(const vehicle_wire::VehicleState& a, const vehicle_wire::VehicleState& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

}  // namespace

int main() {
    using namespace vehicle_wire;

    check(offsetof(VehicleState, role) == 4 && offsetof(VehicleState, id) == 8 && offsetof(VehicleState, position) == 16 &&
              offsetof(VehicleState, rotation) == 28,
          "the wire layout is seq, role, id, position, rotation (64 bytes)");

    VehicleState got{};
    const VehicleState driver = report(kRoleDriver);
    check(decode(proto::bytesOf(driver), got) && same(got, driver), "a driver report round trips");
    const VehicleState passenger = report(kRolePassenger);
    check(decode(proto::bytesOf(passenger), got) && got.role == kRolePassenger && got.id == passenger.id && same(got, passenger),
          "a passenger report round trips with its role and the vehicle's id");

    VehicleState untouched = driver;
    VehicleState unknownRole = report(kRolePassenger + 1);
    check(!decode(proto::bytesOf(unknownRole), untouched) && same(untouched, driver), "an unknown role is refused and leaves the output alone");
    const std::vector<uint8_t> shortPayload(sizeof(VehicleState) - 1);
    check(!decode(shortPayload, untouched), "a short payload is refused");
    const std::vector<uint8_t> longPayload(sizeof(VehicleState) + 1);
    check(!decode(longPayload, untouched), "a long payload is refused");

    std::printf(g_failures ? "FAILED\n" : "all passed\n");
    return g_failures ? 1 : 0;
}
