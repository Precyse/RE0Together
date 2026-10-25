#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lobby_wire.h"
#include "net_client.h"

// The in-game lobby's net side (docs/RE0_NOTES.md "Menus: the native Co-op page"): the launcher's LOBBY_STATE (who is
// in the lobby, the host options, the outfits offered) and what this player changes on the page going back to its
// launcher as OPTION_SET, so the launcher's screens, the lobby and the other player see it through the launcher's own
// path; the host options that apply while the game runs (world pause; the host's character and this player's outfit
// before the game starts) are applied here when the launcher's text changes. Every game tells the others where it is
// (LOBBY_PRESENCE: title, Co-op page, loading, in game; the host's save pick; a guest's check of its copy), and the
// host's start (SESSION_LOAD) reaches the guests through here.
namespace lobby {

struct PeerPresence {
    uint8_t slot;
    lobby_wire::Presence presence;
};

// What the page reads (a copy).
struct View {
    bool haveState = false;  // a LOBBY_STATE arrived
    lobby_wire::LobbyState state;
    std::vector<PeerPresence> peers;  // fresh presences of the other games
    lobby_wire::Presence own{};       // what this game announces
};

// Net thread, every tick: the launcher's text when it changed, then this game's presence (on a change and every 2 s)
// and the host's start while it is in game.
void onNetTick(NetClient& net, const SessionSnapshot& session);

// Net thread: LOBBY_PRESENCE and SESSION_LOAD.
void onFrame(const GameFrame& frame);

// Game thread, from the title: where this game is while no character is controlled (Title, Page, Loading).
void setPlace(lobby_wire::Place place);

// The player changed an option on the page: sent to the launcher. False when the link is down or the text is not
// valid on the wire.
bool sendOption(std::string_view key, std::string_view value);

// Host, after its start committed: every guest at its title loads `slot` with this hash.
void announceStart(int32_t slot, uint32_t slotHash);

// Guest: the host's newest start not taken yet.
std::optional<lobby_wire::SessionLoad> takeStart();

View view();

// Any thread: a partner announced a fresh presence that says it is loading the session.
bool partnerLoading();

// Any thread: the fresh presence of the game in `slot` says it is in play (false when it says another place or has gone
// quiet).
bool inGame(uint8_t slot);

// Any thread: inGame for the session's host.
bool hostInGame();

// Any thread: the lobby name of the player in `slot`, empty when the lobby has none.
std::string nameOf(uint8_t slot);

}  // namespace lobby
