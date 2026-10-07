#include "menu_mirror.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "inventory_sync.h"
#include "log.h"
#include "menu_hold_rule.h"
#include "net_pad.h"
#include "protocol.h"
#include "room_gate.h"
#include "split_rooms.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kResendInterval = std::chrono::seconds(2);
constexpr float kToastSeconds = 2.5f;

std::mutex g_mutex;
std::map<uint8_t, menu_hold_rule::PeerMenu> g_peerMenus;  // slot -> its last MENU_STATE; guarded by g_mutex

menu_mirror::MenuState g_lastSent{};  // net thread only
Clock::time_point g_lastSend;

bool g_frozen = false;  // game thread only

using character_owner::Character;
using UpdateAllFunction = void(__fastcall*)(void* self, void* edx);
using OpenFunction = void(__fastcall*)(void* self, void* edx);
UpdateAllFunction g_originalUpdateAll = nullptr;
OpenFunction g_originalOpen = nullptr;
Character g_focusBeforeMenu = Character::Unknown;  // game thread: focus to give back when the menu closes

bool anyPeerMenu(bool (*test)(const menu_hold_rule::PeerMenu&, menu_hold_rule::Clock::time_point)) {
    const auto now = Clock::now();
    std::lock_guard lock(g_mutex);
    for (const auto& [slot, menu] : g_peerMenus) {
        if (test(menu, now)) return true;
    }
    return false;
}

void setFrozen(bool frozen, bool byMenu) {
    g_frozen = frozen;
    debug_stats::set(debug_stats::Gauge::WorldFrozen, frozen);
    if (frozen && byMenu) debug_stats::count(debug_stats::Counter::MenuFreezes);
    debug_overlay::toast(frozen ? "Waiting for partner" : "Resumed", kToastSeconds);
    logger::write("menu_mirror: world %s%s", frozen ? "frozen" : "resumed",
                  frozen ? (byMenu ? " (peer menu)" : " (room entry)") : "");
}

// Replaces sUnit::updateAll. Runs once per frame even while frozen, so it also ends the freeze. A peer's menu holds this
// world only while the two are together (a peer in another room is not affected by it); a room entry holds it until
// the peer arrives too (room_gate).
void __fastcall updateAllDetour(void* self, void* edx) {
    const bool roomEntry = room_gate::holdsWorld();
    const bool byMenu = anyPeerMenu(menu_hold_rule::holds) && !split_rooms::apart() && !game_state::uiPausesWorld();
    const bool freeze = roomEntry || byMenu;
    if (freeze != g_frozen) setFrozen(freeze, byMenu);
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
    menu_hold_rule::observe(g_peerMenus[frame.slot], state.open != 0, state.phase, Clock::now());
}

bool peerMenuOpen() { return anyPeerMenu(menu_hold_rule::isOpen); }

void onNetTick(NetClient& net) {
    const bool open = game_state::uiPausesWorld();
    const MenuState state{open, static_cast<uint8_t>(open ? game_state::roomPhase() : 0)};
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
