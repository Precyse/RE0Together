#pragma once
// What the adapter tells its own launcher about this game (GAME_STATE, adapters/re0/src/protocol.h kGameState; docs/CONTRACT.md
// 0x0063), pure and unit tested. One line of `key=value` pairs joined by ';':
//   place=title|loading|world;char=off;save=off;slot=-1;copy=ok|refused:<code>|off;peers=<steamid>:<0 live|1 stale|2 mismatch>,...
// CD brings no character and redirects no save, so char is off, save is off and slot is -1. The launcher skips keys it does not know.
#include <cstdint>
#include <string>
#include <vector>

#include "world_gate_rule.h"

namespace game_state_rule {

enum class Place : uint8_t { Title, Loading, World };

inline Place placeOf(world_gate_rule::State state) {
    switch (state) {
        case world_gate_rule::State::Ready: return Place::World;
        case world_gate_rule::State::Loading:
        case world_gate_rule::State::AwaitLocalReady: return Place::Loading;
        default: return Place::Title;
    }
}

inline const char* name(Place place) {
    switch (place) {
        case Place::Title: return "title";
        case Place::Loading: return "loading";
        default: return "world";
    }
}

// Why the other player's copy is not shown (the launcher turns each code into words).
constexpr const char* kCodeSilent = "silent";      // no frame from the peer for 8 s: its copy was removed
constexpr const char* kCodeTemplate = "template";  // the peer's character template could not be made into a copy
constexpr const char* kCodeRecipe = "recipe";      // this build's Req sizes differ from the copy recipes
constexpr const char* kCodeNames = "names";        // a message name the copies need is missing from the catalogue

enum class Link : uint8_t { Live = 0, Stale = 1, Mismatch = 2 };

// Mismatch: the peer's CD_HELLO said its adapter is not ours (nothing is shared with it). Stale: its CD_WORLD stopped.
inline Link linkOf(bool helloSeen, bool helloMatches, bool worldStale) {
    if (helloSeen && !helloMatches) return Link::Mismatch;
    return worldStale ? Link::Stale : Link::Live;
}

struct Peer {
    uint64_t steamId = 0;
    Link link = Link::Live;
};

// What decides whether copies are shown at this moment. `peerProblem` is a code or null.
struct CopyInputs {
    bool configured = true;   // adapter.ini presence=1
    bool namesFound = true;   // init resolved every message name the copies use
    bool recipeDiffers = false;
    const char* peerProblem = nullptr;
};

struct Copy {
    bool off = false;
    const char* refusal = nullptr;  // null: nothing wrong found
};

inline Copy copyOf(const CopyInputs& in) {
    if (!in.configured) return {true, nullptr};
    if (!in.namesFound) return {false, kCodeNames};
    if (in.recipeDiffers) return {false, kCodeRecipe};
    return {false, in.peerProblem};
}

struct Report {
    Place place = Place::Title;
    Copy copy;
    std::vector<Peer> peers;
};

inline std::string format(const Report& report) {
    std::string copy = report.copy.off ? "off" : report.copy.refusal ? std::string("refused:") + report.copy.refusal : "ok";
    std::string out = std::string("place=") + name(report.place) + ";char=off;save=off;slot=-1;copy=" + copy + ";peers=";
    for (size_t i = 0; i < report.peers.size(); ++i) {
        out += (i ? "," : "") + std::to_string(report.peers[i].steamId) + ":" + std::to_string(static_cast<int>(report.peers[i].link));
    }
    return out;
}

}  // namespace game_state_rule
