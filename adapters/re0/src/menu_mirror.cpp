#include "menu_mirror.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

#include "debug_overlay.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "log.h"
#include "protocol.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kResendInterval = std::chrono::seconds(2);
constexpr auto kPeerMenuTimeout = std::chrono::seconds(6);
constexpr float kToastSeconds = 2.5f;

std::mutex g_mutex;
std::map<uint8_t, Clock::time_point> g_peerMenuSeen;  // slot -> last MENU_STATE that said open; guarded by g_mutex

bool g_lastSentOpen = false;  // net thread only
Clock::time_point g_lastSend;

bool g_frozen = false;  // game thread only

using UpdateAllFunction = void(__fastcall*)(void* self, void* edx);
UpdateAllFunction g_originalUpdateAll = nullptr;

bool anyPeerMenuOpen() {
    const auto now = Clock::now();
    std::lock_guard lock(g_mutex);
    for (const auto& [slot, seen] : g_peerMenuSeen) {
        if (now - seen < kPeerMenuTimeout) return true;
    }
    return false;
}

void setFrozen(bool frozen) {
    g_frozen = frozen;
    debug_stats::set(debug_stats::Gauge::WorldFrozen, frozen);
    if (frozen) debug_stats::count(debug_stats::Counter::MenuFreezes);
    debug_overlay::toast(frozen ? "Partner is in a menu" : "Resumed", kToastSeconds);
    logger::write("menu_mirror: world %s", frozen ? "frozen" : "resumed");
}

// Replaces sUnit::updateAll. Runs once per frame even while frozen, so it also ends the freeze.
void __fastcall updateAllDetour(void* self, void* edx) {
    const bool freeze = anyPeerMenuOpen() && !game_state::menuOpen();
    if (freeze != g_frozen) setFrozen(freeze);
    if (freeze) return;
    g_originalUpdateAll(self, edx);
}

}  // namespace

namespace menu_mirror {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgMenuState || frame.payload.size() != 1) return;
    std::lock_guard lock(g_mutex);
    if (frame.payload[0]) g_peerMenuSeen[frame.slot] = Clock::now();
    else g_peerMenuSeen.erase(frame.slot);
}

void onNetTick(NetClient& net) {
    const bool open = game_state::menuOpen();
    const auto now = Clock::now();
    if (open == g_lastSentOpen && !(open && now - g_lastSend >= kResendInterval)) return;
    const uint8_t payload = open;
    if (!net.send(proto::kMsgMenuState, true, proto::kSlotAll, {&payload, sizeof(payload)})) return;
    g_lastSentOpen = open;
    g_lastSend = now;
}

bool enable() {
    return hooks::install("sUnit::updateAll", game::kUnitUpdateAllFunction, reinterpret_cast<void*>(&updateAllDetour),
                          reinterpret_cast<void**>(&g_originalUpdateAll));
}

void uninstall() { hooks::remove(game::kUnitUpdateAllFunction); }

}  // namespace menu_mirror
