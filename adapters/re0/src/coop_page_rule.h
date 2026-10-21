#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "appearance_rule.h"
#include "character_owner.h"
#include "debug_stats.h"
#include "net_client.h"

// Pure text of the Co-op page's status rows (coop_page.cpp, unit tested): the partner, the link, the partner mode, this
// player's character and outfit, and the host. Only labels and values.
namespace coop_page_rule {

constexpr size_t kNameChars = 20;
constexpr uint64_t kStaleMs = 1000;  // the overlay drops a page its owner stopped refreshing (the screen was torn down)

constexpr const char kNone[] = "none";

struct Row {
    std::string label;
    std::string value;
    bool bad;  // drawn red: a down link
};

// What only the game thread can read: this player's character and the model byte of its current outfit.
struct Local {
    character_owner::Character character;
    uint8_t outfitModel;
};

inline std::string clipped(const std::string& name) { return name.empty() ? kNone : name.substr(0, kNameChars); }

inline std::string characterText(character_owner::Character character) {
    switch (character) {
        case character_owner::Character::Billy: return "Billy";
        case character_owner::Character::Rebecca: return "Rebecca";
        default: return kNone;
    }
}

inline std::string outfitText(const Local& local) {
    if (local.character == character_owner::Character::Unknown || local.outfitModel == appearance_rule::kNoOutfit) {
        return kNone;
    }
    const appearance_rule::Outfit* outfit = appearance_rule::find(local.character, local.outfitModel);
    if (outfit) return outfit->name;
    char text[8];
    snprintf(text, sizeof(text), "0x%02x", local.outfitModel);
    return text;
}

inline const PeerInfo* peerInSlot(const debug_stats::Snapshot& stats, uint8_t slot) {
    for (const PeerInfo& peer : stats.peers) {
        if (peer.slot == slot) return &peer;
    }
    return nullptr;
}

inline std::string hostText(const debug_stats::Snapshot& stats) {
    if (!stats.linked) return kNone;
    if (stats.hostSlot == stats.localSlot) return "you";
    const PeerInfo* host = peerInSlot(stats, stats.hostSlot);
    return host ? clipped(host->name) : kNone;
}

inline std::vector<Row> rows(const debug_stats::Snapshot& stats, const Local& local) {
    const PeerInfo* partner = otherPeer(stats.peers, stats.localSlot);
    const char* link = !stats.linked ? "link down" : partner ? "connected" : "waiting for partner";
    return {
        {"Partner", partner ? clipped(partner->name) : kNone, false},
        {"Connection", link, !stats.linked},
        {"Ping", partner ? std::to_string(partner->rttMs) + " ms" : kNone, false},
        {"Mode", stats.stateMode ? "state" : "input", false},
        {"Character", characterText(local.character), false},
        {"Outfit", outfitText(local), false},
        {"Host", hostText(stats), false},
    };
}

// True while the page was refreshed recently enough to still be on screen.
constexpr bool fresh(uint64_t ageMs) { return ageMs <= kStaleMs; }

}  // namespace coop_page_rule
