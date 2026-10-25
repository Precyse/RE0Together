#pragma once
#include <algorithm>

// Pure timing of a banner (toast_queue.cpp, hud_banner.cpp, unit tested): it fades in, holds and fades out within the
// seconds it was given.
namespace toast_rule {

constexpr float kFadeInSeconds = 0.5f;
constexpr float kFadeOutSeconds = 0.6f;

// The banner's opacity `elapsed` seconds into a life of `total`: 0 to 1 over the fade in, 1 while it holds, 1 to 0 over the
// fade out at the end (a short life meets both ramps and takes the lower).
constexpr float alpha(float elapsed, float total) {
    const float rising = elapsed / kFadeInSeconds;
    const float falling = (total - elapsed) / kFadeOutSeconds;
    return std::clamp(std::min(rising, falling), 0.0f, 1.0f);
}

}  // namespace toast_rule
