#pragma once
#include <cstdint>
#include <string>

#include "room_phase.h"
#include "save_load_rule.h"

// What this game tells its own launcher about itself (GAME_STATE, protocol.h kGameState; pure, unit tested). One line of
// `key=value` pairs joined by `;`:
//   place=title|loading|world;char=off;save=ok|refused|off;slot=<int>;coop=0|1;load=none|ok|failed
// The launcher reads place, char, save and slot into the player's card and skips keys it does not know.
namespace game_state_report_rule {

enum class Place : uint8_t { Title, Loading, World };

// The save redirect: off (booted with coop=0), ok (the game's save calls are patched), refused (co-op is on but they are not).
enum class Save : uint8_t { Off, Ok, Refused };

// The forced load of the session's save (title_load): none (not tried this session), ok (the world was started), failed.
enum class Load : uint8_t { None, Ok, Failed };

struct Report {
    Place place = Place::Title;
    Save save = Save::Off;
    int32_t slot = save_load_rule::kUnknownSlot;
    bool coop = false;
    Load load = Load::None;

    bool operator==(const Report&) const = default;
};

// Where the game is, from its room phase (the classification behind session_slot::hostInGame): outside gameplay (boot,
// title, game over, ending) it is at the title, a door or a load of the session's save is loading, else it is in the world.
constexpr Place placeOf(int32_t roomPhase, bool doorActive, bool sessionLoadRunning) {
    if (sessionLoadRunning) return Place::Loading;
    if (!room_phase::isGameplay(roomPhase)) return Place::Title;
    return doorActive || roomPhase == room_phase::DoorLoad ? Place::Loading : Place::World;
}

constexpr const char* name(Place place) {
    switch (place) {
        case Place::Title: return "title";
        case Place::Loading: return "loading";
        case Place::World: break;
    }
    return "world";
}

constexpr const char* name(Save save) {
    switch (save) {
        case Save::Ok: return "ok";
        case Save::Refused: return "refused";
        case Save::Off: break;
    }
    return "off";
}

constexpr const char* name(Load load) {
    switch (load) {
        case Load::Ok: return "ok";
        case Load::Failed: return "failed";
        case Load::None: break;
    }
    return "none";
}

inline std::string format(const Report& report) {
    return std::string("place=") + name(report.place) + ";char=off;save=" + name(report.save) + ";slot=" + std::to_string(report.slot) +
           ";coop=" + (report.coop ? "1" : "0") + ";load=" + name(report.load);
}

}  // namespace game_state_report_rule
