#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

#include "character_owner.h"

// Pure rules of the outfit sync (no game access, unit tested): the outfits offered per character, the APPEARANCE
// wire message and which outfit a machine wants a character to wear.
namespace appearance_rule {

using character_owner::Character;

constexpr uint16_t kMsgAppearance = proto::kFirstGameType + 0x16;  // 0x0116, owner to all, reliable
constexpr uint8_t kNoOutfit = 0;  // no model byte is 0: "keep the save's outfit"
constexpr uint32_t kNoDlc = 0;
constexpr int kHexBase = 16;

// An offered outfit: its model byte and the Steam app id of the DLC pack that carries it (kNoDlc: base game). Only
// the character's own family is offered; Rebecca has no Wesker (the engine only supports Wesker on Billy).
struct Outfit {
    uint8_t model;
    uint32_t dlcAppId;
};

constexpr uint8_t kRebeccaDefault = 0x02;
constexpr uint8_t kBillyDefault = 0x05;
constexpr uint8_t kBillyWesker = 0xa5;

constexpr std::array<Outfit, 10> kRebeccaOutfits = {{{0x02, kNoDlc}, {0x7d, kNoDlc}, {0x81, kNoDlc}, {0x91, kNoDlc},
                                                     {0x71, 381713}, {0x75, 381712}, {0x79, 381710}, {0x85, 381711},
                                                     {0x89, 381713}, {0x8d, 381712}}};
constexpr std::array<Outfit, 5> kBillyOutfits = {{{0x05, kNoDlc}, {0x95, kNoDlc}, {kBillyWesker, kNoDlc},
                                                  {0x99, 381710}, {0x9d, 381711}}};

// The offered outfit with this model for the character, or null (an opposite-sex model, Wesker on Rebecca, an unknown id).
inline const Outfit* find(Character character, uint8_t model) {
    auto search = [model](const auto& list) -> const Outfit* {
        for (const Outfit& outfit : list) {
            if (outfit.model == model) return &outfit;
        }
        return nullptr;
    };
    switch (character) {
        case Character::Rebecca: return search(kRebeccaOutfits);
        case Character::Billy: return search(kBillyOutfits);
        default: return nullptr;
    }
}

inline uint8_t defaultModel(Character character) { return character == Character::Rebecca ? kRebeccaDefault : kBillyDefault; }

// A model byte as written in adapter.ini (hex), or nullopt for an empty or malformed value.
inline std::optional<uint8_t> parseModel(const std::string& text) {
    if (text.empty() || text.size() > 2 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(std::stoul(text, nullptr, kHexBase));
}

// How this machine relates to a character's owner.
enum class Role { Unowned, Local, Remote };

// The outfit this machine wants on a character, kNoOutfit for none. The local ini choice is applied once (`iniApplied`:
// afterwards the suitcase decides and the owner reports it); a remote character wears what its owner reported.
inline uint8_t wantedModel(Role role, bool iniApplied, uint8_t ini, uint8_t reported) {
    switch (role) {
        case Role::Local: return iniApplied ? kNoOutfit : ini;
        case Role::Remote: return reported;
        default: return kNoOutfit;
    }
}

inline bool needsChange(uint8_t current, uint8_t wanted) { return wanted != kNoOutfit && wanted != current; }

// Wire payload of APPEARANCE (0x0116): the character an owner plays and the outfit it wears.
struct Appearance {
    uint8_t character;  // character_owner::Character
    uint8_t model;
    uint8_t reserved[2];
};
static_assert(sizeof(Appearance) == 4);

inline Appearance encode(Character character, uint8_t model) {
    return {static_cast<uint8_t>(character), model, {0, 0}};
}

// The payload, or nullopt for a wrong size or a character that is not Billy or Rebecca.
inline std::optional<Appearance> decode(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(Appearance)) return std::nullopt;
    Appearance appearance;
    std::memcpy(&appearance, payload.data(), sizeof(appearance));
    if (appearance.character != static_cast<uint8_t>(Character::Billy) &&
        appearance.character != static_cast<uint8_t>(Character::Rebecca)) {
        return std::nullopt;
    }
    return appearance;
}

}  // namespace appearance_rule
