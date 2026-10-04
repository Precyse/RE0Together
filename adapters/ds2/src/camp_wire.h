#pragma once
// CAMP_ALERT: the alert phase of the host's enemy camps (command posts), mirrored to the guests (docs/DS2_NOTES.md,
// "Camp alert"). A camp is named by its locator UUID, the same on both machines. The host sends a camp's phase when it
// changes and the whole table every few seconds; a guest sets the same phase on its own camp.
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace camp_wire {

constexpr uint16_t kMsgCampAlert = proto::kFirstGameType + 0x1F;  // 0x011F, host to all, reliable: CampPhase[]
constexpr size_t kUuidSize = 16;
constexpr size_t kMaxCamps = 64;

constexpr int32_t kPhaseAlert = 4;  // EDSSneakingGamePhase: 0 normal, 1 precaution, 2 caution, 3 evasion, 4 alert

struct CampPhase {
    uint8_t uuid[kUuidSize];
    int32_t phase;
    uint32_t reserved;
};
static_assert(sizeof(CampPhase) == 24);

inline std::vector<uint8_t> encode(std::span<const CampPhase> camps) {
    std::vector<uint8_t> out(camps.size() * sizeof(CampPhase));
    if (!camps.empty()) std::memcpy(out.data(), camps.data(), out.size());
    return out;
}

// False for a payload that is not a whole number of entries or holds more than kMaxCamps.
inline bool decode(std::span<const uint8_t> payload, std::vector<CampPhase>& out) {
    if (payload.size() % sizeof(CampPhase) != 0 || payload.size() / sizeof(CampPhase) > kMaxCamps) return false;
    out.resize(payload.size() / sizeof(CampPhase));
    if (!out.empty()) std::memcpy(out.data(), payload.data(), payload.size());
    return true;
}

}  // namespace camp_wire
