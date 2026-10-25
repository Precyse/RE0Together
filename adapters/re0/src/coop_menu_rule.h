#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lobby_wire.h"
#include "native_menu_rule.h"

// Pure rules of the Co-op page (coop_menu.cpp, unit tested; docs/RE0_NOTES.md "Menus: the native Co-op page"): its
// rows, the text of each, which a guest only reads, and how a choice steps through its values. Like the launcher's
// hosting screen: the players with their role and readiness, this player's character (the host's choice) and outfit, the
// host options, the save the session plays and the host's start.
namespace coop_menu_rule {

using lobby_wire::Place;
using native_menu_rule::PageView;
using native_menu_rule::RowView;

constexpr std::string_view kMenuId = "coop";
constexpr const char kHeader[] = "Co-op";

enum Row : int { kHostPlayer, kGuestPlayer, kCharacter, kOutfit, kWorldPause, kPartnerMode, kSave, kStart, kRowCount };
static_assert(kRowCount == static_cast<int>(native_menu_rule::kMaxRows));

constexpr uint64_t kPresenceStaleMs = 6000;  // a game that announced nothing for this long is away (presence every 2 s)

// A member as the page shows it: from the launcher's LOBBY_STATE, and how its game last reported itself.
struct Player {
    std::string name;
    bool host = false;
    bool local = false;
    std::optional<lobby_wire::Presence> presence;  // fresh presence (this machine's own for the local player)
};

// The state word of a player: where its game is, and for a guest on the page whether its save copy is the host's.
inline std::string stateText(const Player& player) {
    if (!player.presence) return "Away";
    switch (static_cast<Place>(player.presence->place)) {
        case Place::InGame: return "In game";
        case Place::Loading: return "Loading";
        case Place::Title: return "In menu";
        case Place::Page: break;
    }
    const uint8_t flags = player.presence->flags;
    const bool differs = (flags & lobby_wire::kSaveChecked) != 0 && (flags & lobby_wire::kSaveMatches) == 0;
    return !player.host && differs ? "Save differs" : "Ready";
}

inline RowView playerRow(const std::optional<Player>& player) {
    if (!player) return {"Open", "", true, false};
    return {player->local ? player->name + " (you)" : player->name,
            std::string(player->host ? "Host" : "Guest") + " / " + stateText(*player), true, false};
}

// How the host's start goes on this machine.
enum class StartStatus : uint8_t { Idle, Loading, WaitingForSave };

inline std::string startStatusText(StartStatus status) {
    switch (status) {
        case StartStatus::Loading: return "Loading";
        case StartStatus::WaitingForSave: return "Waiting for save";
        case StartStatus::Idle: break;
    }
    return "";
}

// Everything the page shows, gathered by coop_menu.cpp.
struct Inputs {
    bool isHost = false;
    std::optional<Player> host;
    std::optional<Player> guest;
    std::string character;    // this player's character label
    std::string outfit;       // its outfit's label
    bool outfitChoosable = false;
    std::string worldPause;   // the host option's label
    std::string partnerMode;
    std::string save;         // the picked slot's label, "None" without a pick
    bool savePicked = false;
    bool savesOffered = false;  // host: the save manager's blocks were readable
    StartStatus start = StartStatus::Idle;
};

constexpr const char kNone[] = "None";

inline PageView view(const Inputs& in) {
    const bool guest = !in.isHost;
    const bool busy = in.start != StartStatus::Idle;
    PageView page;
    page.header = kHeader;
    page.rows[kHostPlayer] = playerRow(in.host);
    page.rows[kGuestPlayer] = playerRow(in.guest);
    page.rows[kCharacter] = {"Character", in.character, true, guest || busy};
    page.rows[kOutfit] = {"Outfit", in.outfit, true, !in.outfitChoosable || busy};
    page.rows[kWorldPause] = {"World pause", in.worldPause, true, guest || busy};
    page.rows[kPartnerMode] = {"Partner mode", in.partnerMode, true, true};
    page.rows[kSave] = {"Save", in.save, true, guest || !in.savesOffered || busy};
    const std::string status = startStatusText(in.start);
    page.rows[kStart] = guest ? RowView{in.start == StartStatus::Idle ? "Waiting for host" : status, "", true, true}
                              : RowView{"Start", status, true, !in.savePicked || busy};
    return page;
}

// The index after `index` in a list of `count` values, one step left (-1) or right (+1), wrapping. An index outside the
// list (the current value is not offered) starts from the first value.
constexpr int stepIndex(int index, int count, int step) {
    if (count <= 0) return -1;
    if (index < 0 || index >= count) return 0;
    return ((index + step) % count + count) % count;
}

}  // namespace coop_menu_rule
