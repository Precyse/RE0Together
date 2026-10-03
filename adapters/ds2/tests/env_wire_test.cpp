// env_wire: the WORLD_ENV payload checks and the time-of-day distance (no game).
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/env_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<uint8_t> bytes(const env_wire::WorldEnv& env) {
    std::vector<uint8_t> out(sizeof(env));
    std::memcpy(out.data(), &env, sizeof(env));
    return out;
}

}  // namespace

int main() {
    using namespace env_wire;
    WorldEnv sent{};
    sent.flags = kFlagTimePaused;
    sent.slot = 2;
    sent.timeOfDay = 18.25f;
    sent.day = 31;
    sent.forecastClock = 58103.0f;
    sent.nextThreshold = 58630.0f;
    for (int i = 0; i < kRegionCount; ++i) sent.regionType[i] = static_cast<uint8_t>(i % 6);

    WorldEnv got{};
    check(decode(bytes(sent), got), "a payload decodes");
    check(std::memcmp(&got, &sent, sizeof(sent)) == 0, "every field survives");

    std::vector<uint8_t> shorter = bytes(sent);
    shorter.pop_back();
    check(!decode(shorter, got), "a short payload is rejected");
    std::vector<uint8_t> longer = bytes(sent);
    longer.push_back(0);
    check(!decode(longer, got), "a long payload is rejected");

    WorldEnv bad = sent;
    bad.timeOfDay = 24.0f;
    check(!decode(bytes(bad), got), "a time past the end of the day is rejected");
    bad.timeOfDay = -1.0f;
    check(!decode(bytes(bad), got), "a negative time is rejected");
    bad.timeOfDay = std::numeric_limits<float>::quiet_NaN();
    check(!decode(bytes(bad), got), "a NaN time is rejected");

    check(hoursApart(1.0f, 3.0f) == 2.0f, "two times in the same stretch of the day");
    check(hoursApart(23.5f, 0.5f) == 1.0f, "the distance wraps over midnight");
    check(hoursApart(6.0f, 18.0f) == 12.0f, "opposite times are half a day apart");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
