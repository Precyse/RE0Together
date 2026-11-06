#pragma once
// What the plugin tells its own launcher about this game (GAME_STATE, adapters/re0/src/protocol.h kGameState) and the
// problem lines the overlay shows (pure, unit tested). Text, one line of `key=value` pairs joined by ';':
//   place=title|loading|world;char=ok|none|mismatch|refused|off;save=ok|refused|off;slot=<int>;peers=<steamid>:<0 live|1 stale>,...
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "world_gate_rule.h"

namespace game_state_rule {

enum class Place : uint8_t { Title, Loading, World };

// The co-op character of a guest: off (not a splice guest), ok (nothing wrong found, also before the first load),
// none (no readable profile of this build), mismatch (the Arisen the game built is not the profile's), refused (a piece or
// the whole splice was not taken).
enum class Char : uint8_t { Off, Ok, None, Mismatch, Refused };

// The save redirect: off (no other save folder expected), ok (verified), refused (expected, not verified).
enum class Save : uint8_t { Off, Ok, Refused };

constexpr int kSlotUnknown = -1;

struct Peer {
    uint64_t steamId;
    bool stale;
};

struct Report {
    Place place = Place::Title;
    Char character = Char::Off;
    Save save = Save::Off;
    int slot = kSlotUnknown;
    std::vector<Peer> peers;
};

inline Place placeOf(world_gate_rule::Place place) {
    using Gate = world_gate_rule::Place;
    switch (place) {
        case Gate::Title: return Place::Title;
        case Gate::Loading:
        case Gate::Travel: return Place::Loading;
        default: return Place::World;
    }
}

inline const char* name(Place place) {
    switch (place) {
        case Place::Title: return "title";
        case Place::Loading: return "loading";
        default: return "world";
    }
}

inline const char* name(Char character) {
    switch (character) {
        case Char::Ok: return "ok";
        case Char::None: return "none";
        case Char::Mismatch: return "mismatch";
        case Char::Refused: return "refused";
        default: return "off";
    }
}

inline const char* name(Save save) {
    switch (save) {
        case Save::Ok: return "ok";
        case Save::Refused: return "refused";
        default: return "off";
    }
}

inline std::string format(const Report& report) {
    std::string out = std::string("place=") + name(report.place) + ";char=" + name(report.character) + ";save=" + name(report.save) +
                      ";slot=" + std::to_string(report.slot) + ";peers=";
    for (size_t i = 0; i < report.peers.size(); ++i) {
        out += (i ? "," : "") + std::to_string(report.peers[i].steamId) + (report.peers[i].stale ? ":1" : ":0");
    }
    return out;
}

// Why a guest's character was not loaded (the overlay's "Your character could not be loaded: <why>"); null when it was.
inline const char* characterProblem(Char character) {
    switch (character) {
        case Char::None: return "no saved character of this game version";
        case Char::Mismatch: return "the level in the game differs from it";
        case Char::Refused: return "the game did not accept it";
        default: return nullptr;
    }
}

// A partner's line on the overlay: their name and where they are; the distance only when they are in the world.
inline std::string partnerLine(const std::string& name, bool stale, Place place, std::optional<double> meters) {
    if (stale) return name + ": connection lost";
    switch (place) {
        case Place::Title: return name + ": title";
        case Place::Loading: return name + ": loading";
        default: break;
    }
    return name + ": in world" + (meters ? ", " + std::to_string(static_cast<int>(*meters + 0.5)) + " m" : "");
}

// The overlay's lines about this machine, in order; empty when all is well. `hostSlot` is the host's load slot when the
// launcher gave one (a guest only): the line shows at the title before a load and after a load of another slot.
inline std::vector<std::string> problems(const Report& report, int hostSlot) {
    std::vector<std::string> out;
    if (const char* why = characterProblem(report.character)) out.push_back(std::string("Your character could not be loaded: ") + why);
    if (report.save == Save::Refused) out.push_back("Save not protected: quit to the title");
    const bool notLoadedYet = report.slot == kSlotUnknown && report.place == Place::Title;
    if (hostSlot != kSlotUnknown && (notLoadedYet || (report.slot != kSlotUnknown && report.slot != hostSlot))) {
        out.push_back("Load slot " + std::to_string(hostSlot) + " like the host");
    }
    return out;
}

}  // namespace game_state_rule
