#pragma once
// WORLD_ENV: the host's time of day and weather, sent to the guests so both worlds show the same sky. Time of day is
// the GameWorldTimeState, weather the DSWeatherManager's forecast table; neither is a fact, so FACT_SET does not
// carry them (docs/DS2_NOTES.md, "Weather and time of day").
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace env_wire {

constexpr uint16_t kMsgWorldEnv = proto::kFirstGameType + 13;  // 0x010D, host to all, reliable: WorldEnv
constexpr int kRegionCount = 64;
constexpr uint8_t kRegionNone = 0xE;  // a region entry that holds no weather
constexpr uint8_t kFlagTimePaused = 1;
constexpr float kHoursPerDay = 24.0f;

#pragma pack(push, 1)
struct WorldEnv {
    uint8_t flags;
    uint8_t slot;         // the host's forecast buffer index (the guest reads its own)
    float timeOfDay;      // hours, 0..24
    int32_t day;
    float forecastClock;  // seconds
    float nextThreshold;  // seconds
    uint8_t regionType[kRegionCount];
};
#pragma pack(pop)
static_assert(sizeof(WorldEnv) == 82);

// False for a payload of the wrong size or with a time outside the day (nothing is returned).
inline bool decode(std::span<const uint8_t> payload, WorldEnv& out) {
    if (payload.size() != sizeof(WorldEnv)) return false;
    WorldEnv env;
    std::memcpy(&env, payload.data(), sizeof(env));
    if (!(env.timeOfDay >= 0.0f && env.timeOfDay < kHoursPerDay)) return false;
    out = env;
    return true;
}

// Shortest distance between two times of day, in hours.
inline float hoursApart(float a, float b) {
    const float d = a > b ? a - b : b - a;
    return d < kHoursPerDay - d ? d : kHoursPerDay - d;
}

}  // namespace env_wire
