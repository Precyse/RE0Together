#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lobby_wire.h"
#include "native_menu_rule.h"

// Pure rules of the Co-op page and its Options page (coop_menu.cpp, unit tested; docs/RE0_NOTES.md "Menus: the native
// Co-op page"): their rows, the text of each, which a guest only reads, and how a choice steps through its values. The
// page: this player's character (the host's choice), outfit, the save the session plays, the Options page and the host's
// start. The players themselves are drawn beside the page (coop_scene_rule.h).
namespace coop_menu_rule {

using lobby_wire::Place;
using native_menu_rule::PageView;
using native_menu_rule::RowView;

constexpr std::string_view kMenuId = "coop";
constexpr std::string_view kOptionsMenuId = "coop.options";
constexpr const char kHeader[] = "Co-op";
constexpr const char kOptionsHeader[] = "Options";

constexpr uint64_t kPresenceStaleMs = 6000;  // a game that announced nothing for this long is away (presence every 2 s)

enum Row : int { kCharacter, kOutfit, kSave, kOptions, kStart, kRowCount };
static_assert(kRowCount <= static_cast<int>(native_menu_rule::kMaxRows));

enum OptionsRow : int { kWorldPause, kBack, kOptionsRowCount };
static_assert(kOptionsRowCount <= static_cast<int>(native_menu_rule::kMaxRows));

// A member as the page shows it: from the launcher's LOBBY_STATE, and how its game last reported itself.
struct Player {
    std::string name;
    bool host = false;
    bool local = false;
    std::optional<lobby_wire::Presence> presence;  // fresh presence (this machine's own for the local player)
    std::string outfitLabel;                       // the launcher's label of its outfit; empty without the launcher's text
};

// Whether a player's game waits on the Co-op page and, for a guest, holds the host's save.
inline bool isReady(const Player& player) {
    if (!player.presence || static_cast<Place>(player.presence->place) != Place::Page) return false;
    const uint8_t flags = player.presence->flags;
    const bool differs = (flags & lobby_wire::kSaveChecked) != 0 && (flags & lobby_wire::kSaveMatches) == 0;
    return player.host || !differs;
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
    std::string character;    // this player's character label
    std::string outfit;       // its outfit's label
    bool outfitChoosable = false;
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
    page.rows[kCharacter] = {"Character", in.character, true, guest || busy};
    page.rows[kOutfit] = {"Outfit", in.outfit, true, !in.outfitChoosable || busy};
    page.rows[kSave] = {"Save", in.save, true, guest || !in.savesOffered || busy};
    page.rows[kOptions] = {"Options", "", true, false};
    const std::string status = startStatusText(in.start);
    page.rows[kStart] = guest ? RowView{in.start == StartStatus::Idle ? "Waiting for host" : status, "", true, true}
                              : RowView{"Start", status, true, !in.savePicked || busy};
    return page;
}

// The Options page: the host's settings (a guest reads them) and the way back.
inline PageView optionsView(bool isHost, const std::string& worldPause) {
    PageView page;
    page.header = kOptionsHeader;
    page.rows[kWorldPause] = {"Pause in Menus", worldPause, true, !isHost};
    page.rows[kBack] = {"Back", "", true, false};
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
