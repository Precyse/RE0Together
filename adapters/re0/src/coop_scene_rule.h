#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "coop_menu_rule.h"
#include "pulse_rule.h"

// Pure rules of the characters beside the Co-op page (coop_scene.cpp, hud_coop_scene.cpp, unit tested): which outfit
// picture a player's character wears, the words under it, the open seat's silhouette and when the page stops refreshing.
namespace coop_scene_rule {

constexpr uint64_t kStaleMs = 300;  // the page rewrites the scene every frame; one older than this is no longer open
constexpr float kSilhouetteMinAlpha = 0.25f;
constexpr float kSilhouetteMaxAlpha = 0.45f;
constexpr float kSilhouetteSeconds = 3.0f;
constexpr char kOpenRole[] = "Open";
constexpr char kYou[] = "You";
constexpr char kRebeccaDefaultOutfit[] = "02";  // the picture for the saved outfit (the empty value)
constexpr char kBillyDefaultOutfit[] = "05";

// The outfit whose picture is drawn for a character wearing `outfit` (an outfit option value): the outfit itself, or for the
// saved outfit (empty) the character's default one.
inline std::string pictureValue(bool billy, std::string_view outfit) {
    if (!outfit.empty()) return std::string(outfit);
    return billy ? kBillyDefaultOutfit : kRebeccaDefaultOutfit;
}

inline std::string whoIs(const coop_menu_rule::Player& player) { return player.local ? std::string(kYou) : player.name; }

// The second line under the host's character.
inline std::string hostRole(const coop_menu_rule::Player& host) { return whoIs(host) + " / Host"; }

// The second line under the guest's character.
inline std::string guestRole(const coop_menu_rule::Player& guest) {
    return whoIs(guest) + (coop_menu_rule::isReady(guest) ? " / Ready" : " / Not ready");
}

// The open seat's silhouette opacity at `seconds`: a slow swell between the two ends, starting at the low one.
inline float silhouetteAlpha(float seconds) {
    return pulse_rule::between(kSilhouetteMinAlpha, kSilhouetteMaxAlpha, seconds, kSilhouetteSeconds);
}

constexpr bool fresh(uint64_t ageMs) { return ageMs <= kStaleMs; }

}  // namespace coop_scene_rule
