#pragma once
#include <optional>
#include <string_view>

#include "character_owner.h"

// The option keys and values the adapter shares with the launcher's profile (launcher/games/re0.json): what adapter.ini
// holds (config.cpp) and what LOBBY_STATE / OPTION_SET carry (lobby.cpp, coop_menu.cpp). Pure.
namespace option_values {

constexpr std::string_view kCharacterKey = "character";
constexpr std::string_view kWorldPauseKey = "world_pause";
constexpr std::string_view kPartnerModeKey = "partner_mode";
constexpr std::string_view kLoadSlotKey = "load_slot";
constexpr std::string_view kOutfitRebeccaKey = "outfit_rebecca";
constexpr std::string_view kOutfitBillyKey = "outfit_billy";

constexpr std::string_view kRebecca = "rebecca";
constexpr std::string_view kBilly = "billy";
constexpr std::string_view kWorldPauseNever = "never";
constexpr std::string_view kWorldPauseMenus = "menus";

inline std::optional<character_owner::Character> characterOf(std::string_view value) {
    if (value == kRebecca) return character_owner::Character::Rebecca;
    if (value == kBilly) return character_owner::Character::Billy;
    return std::nullopt;
}

constexpr std::string_view valueOf(character_owner::Character character) {
    return character == character_owner::Character::Billy ? kBilly : kRebecca;
}

constexpr std::string_view outfitKey(character_owner::Character character) {
    return character == character_owner::Character::Billy ? kOutfitBillyKey : kOutfitRebeccaKey;
}

}  // namespace option_values
