#include "menu_mirror.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "inventory_sync.h"
#include "live_menus.h"
#include "lobby.h"
#include "log.h"
#include "menu_hold_rule.h"
#include "net_pad.h"
#include "protocol.h"
#include "pose_wire.h"
#include "split_rooms.h"
#include "wait_notice.h"
#include "wait_notice_rule.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kResendInterval = std::chrono::seconds(2);
constexpr float kToastSeconds = 2.5f;

std::mutex g_mutex;
std::map<uint8_t, menu_hold_rule::PeerMenu> g_peerMenus;  // slot -> its last MENU_STATE; guarded by g_mutex

menu_mirror::MenuState g_lastSent{};  // net thread only
Clock::time_point g_lastSend;

bool g_frozen = false;  // game thread only
bool g_inGame = false;               // game thread: a character is controlled
uint32_t g_inGameSinceMs = 0;        // game thread: when it last became so
bool g_pausedByPeer = false;         // game thread: the current freeze is a partner's screen

using character_owner::Character;
using UpdateAllFunction = void(__fastcall*)(void* self, void* edx);
using OpenFunction = void(__fastcall*)(void* self, void* edx);
UpdateAllFunction g_originalUpdateAll = nullptr;
OpenFunction g_originalOpen = nullptr;
Character g_focusBeforeMenu = Character::Unknown;  // game thread: focus to give back when the menu closes

// The first peer whose screen passes `test`, with its slot.
std::optional<std::pair<uint8_t, menu_hold_rule::PeerMenu>> peerWhere(bool (*test)(const menu_hold_rule::PeerMenu&,
                                                                                     menu_hold_rule::Clock::time_point)) {
    const auto now = Clock::now();
    std::lock_guard lock(g_mutex);
    for (const auto& [slot, menu] : g_peerMenus) {
        if (test(menu, now)) return std::make_pair(slot, menu);
    }
    return std::nullopt;
}

bool anyPeerMenu(bool (*test)(const menu_hold_rule::PeerMenu&, menu_hold_rule::Clock::time_point)) {
    return peerWhere(test).has_value();
}

void setFrozen(bool frozen) {
    g_frozen = frozen;
    debug_stats::set(debug_stats::Gauge::WorldFrozen, frozen);
    if (frozen) debug_stats::count(debug_stats::Counter::MenuFreezes);
    if (!frozen && g_pausedByPeer) debug_overlay::toast("Resumed.", kToastSeconds, toast_queue::Style::Line);
    if (!frozen) wait_notice::clear();
    logger::write("menu_mirror: world %s", frozen ? "frozen" : "resumed");
}

// Milliseconds this game has had a controlled character, 0 while it has none.
uint32_t inGameMs() {
    const bool inGame = game::controlled() != 0;
    if (inGame && !g_inGame) g_inGameSinceMs = pose_wire::clockMs();
    g_inGame = inGame;
    return inGame ? pose_wire::clockMs() - g_inGameSinceMs : 0;
}

// Why the world waits for a partner now, with the notice that says so; Notice::None when it does not.
wait_notice_rule::Notice waitReason(std::string& text) {
    using wait_notice_rule::Notice;
    const uint32_t playedMs = inGameMs();
    if (split_rooms::apart() || live_menus::localScreen() == live_menu_rule::Screen::Holding) return Notice::None;
    if (const auto peer = peerWhere(menu_hold_rule::holds)) {
        const Notice notice = wait_notice_rule::holdNotice(peer->second.phase);
        text = wait_notice_rule::text(notice, lobby::nameOf(peer->first));
        return notice;
    }
    if (wait_notice_rule::loadHolds(lobby::partnerLoading(), playedMs)) {
        text = wait_notice_rule::text(Notice::PartnerLoading, {});
        return Notice::PartnerLoading;
    }
    return Notice::None;
}

// Replaces sUnit::updateAll. Runs once per frame even while frozen, so it also ends the freeze. A peer in another room
// is not affected by this world, so it is held only while the two are together. A local screen that holds the world
// already stops it.
void __fastcall updateAllDetour(void* self, void* edx) {
    live_menus::onUnitUpdate();
    std::string text;
    const wait_notice_rule::Notice reason = waitReason(text);
    const bool freeze = reason != wait_notice_rule::Notice::None;
    if (freeze) {
        g_pausedByPeer = reason != wait_notice_rule::Notice::PartnerLoading;
        wait_notice::show(reason, std::move(text));
    }
    if (freeze != g_frozen) setFrozen(freeze);
    if (freeze) return;
    g_originalUpdateAll(self, edx);
}

void __fastcall openDetour(void* self, void* edx) {
    const Character focused = character_owner::identify(game::controlled());
    const Character partner = character_owner::identify(game::partner());
    if (net_pad::active() && !character_owner::isLocalOwned(focused) && character_owner::isLocalOwned(partner)) {
        g_focusBeforeMenu = focused;
        character_owner::focus(partner);
        logger::write("menu_mirror: menu opened for %s", character_owner::name(partner));
    }
    inventory_sync::onMenuOpen();
    g_originalOpen(self, edx);
}

void onTick() {
    if (g_focusBeforeMenu == Character::Unknown || game_state::menuOpen()) return;
    character_owner::focus(g_focusBeforeMenu);
    g_focusBeforeMenu = Character::Unknown;
}

}  // namespace

namespace menu_mirror {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgMenuState || frame.payload.size() != sizeof(MenuState)) return;
    MenuState state;
    std::memcpy(&state, frame.payload.data(), sizeof(state));
    std::lock_guard lock(g_mutex);
    menu_hold_rule::observe(g_peerMenus[frame.slot], static_cast<live_menu_rule::Screen>(state.open), state.phase, Clock::now());
}

bool peerMenuOpen() { return anyPeerMenu(menu_hold_rule::isOpen); }

bool releasePeerHold() {
    std::lock_guard lock(g_mutex);
    bool dropped = false;
    for (auto& [slot, menu] : g_peerMenus) {
        dropped = dropped || menu.holding;
        menu.holding = false;
    }
    return dropped;
}

void onNetTick(NetClient& net) {
    const live_menu_rule::Screen screen = live_menus::localScreen();
    const bool open = screen != live_menu_rule::Screen::None;
    const MenuState state{static_cast<uint8_t>(screen), static_cast<uint8_t>(open ? game_state::roomPhase() : 0)};
    const auto now = Clock::now();
    const bool changed = state.open != g_lastSent.open || state.phase != g_lastSent.phase;
    if (!changed && !(open && now - g_lastSend >= kResendInterval)) return;
    if (!net.send(proto::kMsgMenuState, true, proto::kSlotAll, proto::bytesOf(state))) return;
    g_lastSent = state;
    g_lastSend = now;
}

bool enable() {
    game_tick::addCallback("menu_mirror", onTick);
    return hooks::install("sUnit::updateAll", game::kUnitUpdateAllFunction, reinterpret_cast<void*>(&updateAllDetour),
                          reinterpret_cast<void**>(&g_originalUpdateAll)) &&
           hooks::install("sSubMenu::open", game::kSubMenuOpenFunction, reinterpret_cast<void*>(&openDetour),
                          reinterpret_cast<void**>(&g_originalOpen));
}

void uninstall() {
    hooks::remove(game::kUnitUpdateAllFunction);
    hooks::remove(game::kSubMenuOpenFunction);
}

}  // namespace menu_mirror
