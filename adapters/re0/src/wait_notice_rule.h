#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "menu_hold_rule.h"
#include "pulse_rule.h"

// Pure rules of the on-screen wait notices (wait_notice.cpp, unit tested): when this machine's world waits for a partner
// still loading the session, what the notice says while a partner's screen holds the world, and the motion of the loading
// notice.
namespace wait_notice_rule {

enum class Notice : uint8_t {
    None,
    PartnerLoading,  // the session just started here and a partner is still loading it
    PartnerPaused,   // a partner's pause menu, save screen or menu holds the world
    PartnerBusy,     // a partner reads a message or watches a cutscene
};

// A game in play this long no longer waits for a loading partner: that partner joins a game already running.
constexpr uint32_t kLoadWaitWindowMs = 60000;

constexpr bool loadHolds(bool partnerLoading, uint32_t inGameMs) { return partnerLoading && inGameMs < kLoadWaitWindowMs; }

// The notice of a partner's screen that holds the world, by the room phase it said.
constexpr Notice holdNotice(int32_t phase) {
    return menu_hold_rule::unlimited(phase) ? Notice::PartnerBusy : Notice::PartnerPaused;
}

constexpr float kPulseMinAlpha = 0.55f;  // the loading notice fades between these two over kPulseSeconds
constexpr float kPulseMaxAlpha = 0.9f;
constexpr float kPulseSeconds = 2.4f;
constexpr float kSweepSeconds = 1.8f;    // the bright segment crosses the loading notice's hairline once in this time

// The loading notice's opacity at `seconds`: a smooth swell, starting at the dim end.
inline float pulseAlpha(float seconds) { return pulse_rule::between(kPulseMinAlpha, kPulseMaxAlpha, seconds, kPulseSeconds); }

// Where the bright segment starts along a hairline of `ruleWidth` at `seconds`: from the left end to the right end.
inline float sweepStart(float seconds, float ruleWidth, float segmentWidth) {
    return std::fmod(seconds, kSweepSeconds) / kSweepSeconds * (ruleWidth - segmentWidth);
}

// The notice's text; `name` is the partner's (empty when the lobby has none).
inline std::string text(Notice notice, std::string_view name) {
    const std::string who = name.empty() ? std::string("Your partner") : std::string(name);
    switch (notice) {
        case Notice::PartnerLoading: return "Waiting for partner";
        case Notice::PartnerPaused: return who + " has paused";
        case Notice::PartnerBusy: return "Waiting for " + (name.empty() ? std::string("partner") : std::string(name));
        case Notice::None: break;
    }
    return {};
}

}  // namespace wait_notice_rule
