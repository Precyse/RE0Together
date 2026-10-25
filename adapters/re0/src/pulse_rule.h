#pragma once
#include <cmath>

// A smooth swell for the HUD's pulsing parts (the loading notice, the open seat's silhouette); pure, tested through their rules.
namespace pulse_rule {

constexpr float kTwoPi = 6.28318530718f;

// `low` at the start of each period, `high` at its middle, a cosine between.
inline float between(float low, float high, float seconds, float periodSeconds) {
    const float swell = 0.5f - 0.5f * std::cos(kTwoPi * seconds / periodSeconds);
    return low + (high - low) * swell;
}

}  // namespace pulse_rule
