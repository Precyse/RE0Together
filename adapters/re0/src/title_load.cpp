#include "title_load.h"

#include <atomic>
#include <chrono>
#include <optional>

#include "character_owner.h"
#include "debug_overlay.h"
#include "game.h"
#include "lobby.h"
#include "log.h"
#include "native_page.h"
#include "save_blocks.h"
#include "save_index_rule.h"
#include "session_slot.h"
#include "title_load_rule.h"

namespace {

using Clock = std::chrono::steady_clock;
using title_load_rule::Order;
using title_load_rule::Stage;
using title_load_rule::Verdict;

constexpr float kFailedToastSeconds = 4.0f;
constexpr int32_t kWholeSequence = 0;  // uGUI::isSequenceFinished's argument as the main menu input asks it

// Game thread only, like everything below but the atomics.
std::optional<Order> g_order;
Stage g_stage = Stage::Idle;
uintptr_t g_title = 0;
int g_tries = 0;
Clock::time_point g_firstRequest;
Clock::time_point g_stageSince;
std::atomic<bool> g_follows{false};
save_load_rule::Order g_launcherPick;  // set once before the game runs
bool g_launcherPickDone = false;       // started or skipped: a later main menu (game over, quit) is the player's
std::atomic<bool> g_busy{false};  // g_order is set (read by the net thread's auto_join)

void setOrder(const Order& order) {
    g_order = order;
    g_busy = true;
}

void clearOrder() {
    g_order.reset();
    g_busy = false;
}

uint64_t msSince(Clock::time_point since) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}

void enter(Stage stage) {
    g_stage = stage;
    g_stageSince = Clock::now();
}

uintptr_t saveManager() { return game::readPointer(game::kSaveManagerGlobal); }

bool saveBusy() {
    const uintptr_t manager = saveManager();
    return !manager || game::callThiscall<bool>(game::kSaveManagerBusyFunction, reinterpret_cast<void*>(manager));
}

void setTitleState(uint32_t state) { game::writeMemory(g_title + game::kTitleStateOffset, state); }

void fail(const char* why) {
    logger::write("title_load: slot %d not started (%s), the main menu is back", g_order->slot, why);
    debug_overlay::toast("The session's save did not load.", kFailedToastSeconds, toast_queue::Style::Panel);
    clearOrder();
    enter(Stage::Idle);
    setTitleState(title_load_rule::kStateMainMenuOpen);
}

// The title is ours: parked (its dispatch does nothing), the screen fading to black.
void takeTitle(bool fade) {
    setTitleState(title_load_rule::kStateParked);
    if (fade) {
        const uintptr_t fadeObject = game::fadeObject();
        if (fadeObject) game::callThiscall<void>(game::kFadeStartFunction, reinterpret_cast<void*>(fadeObject), game::kMenuFadeSeconds);
    }
    g_tries = 0;
    lobby::setPlace(lobby_wire::Place::Loading);
    enter(Stage::FadingOut);
    logger::write("title_load: the title is parked for slot %d (%s)", g_order->slot, g_order->host ? "host start" : "following the host");
}

// What stateOpenLoadGame 0x5edeb0 writes before the load list opens, then the request (it passes session_slot's hook).
void request() {
    const uintptr_t manager = saveManager();
    if (saveBusy()) return;
    if (g_tries == 0) {
        game::writeMemory(manager + game::kSaveManagerLoadModeOffset, uint8_t{0});
        g_firstRequest = Clock::now();
    }
    if (!game::callThiscall<bool>(game::kSaveManagerLoadFunction, reinterpret_cast<void*>(manager), g_order->slot)) {
        if (title_load_rule::requestGivesUp(msSince(g_stageSince))) fail("the save manager refused the request");
        return;
    }
    ++g_tries;
    logger::write("title_load: load of slot %d requested (try %d)", g_order->slot, g_tries);
    enter(Stage::Loading);
}

// stateLoadGameWait 0x5eca90 for Continue: start mode (2 for a cleared save) and Wesker on sGameInfo, then the area switch
// to the game; GameStart builds the world from the loaded slot.
void commit(uint32_t hash) {
    const Order order = *g_order;
    const std::optional<save_index_rule::Summary> loaded = save_blocks::loaded(order.slot);
    const uintptr_t gameInfo = game::readPointer(game::kGameInfoGlobal);
    const uintptr_t areas = game::readPointer(game::kAreaManagerGlobal);
    const uintptr_t areaSwitch = areas ? game::readPointer(game::readPointer(areas) + game::kAreaSwitchSlot) : 0;
    if (!loaded || !gameInfo || !areaSwitch) {
        fail("sGameInfo or the area manager missing");
        return;
    }
    const int32_t mode = save_index_rule::startMode(*loaded);
    game::callThiscall<void>(game::kGameInfoWeskerFunction, reinterpret_cast<void*>(gameInfo), static_cast<uint32_t>(loaded->wesker));
    game::callThiscall<void>(game::kGameInfoStartModeFunction, reinterpret_cast<void*>(gameInfo), mode);
    logger::write("title_load: slot %d loaded (hash %08x), start mode %d, wesker %d, switching to the game", order.slot, hash, mode,
                  loaded->wesker ? 1 : 0);
    clearOrder();
    enter(Stage::Started);
    game::callThiscall<void>(areaSwitch, reinterpret_cast<void*>(areas), game::kGameArea);
    if (order.host) lobby::announceStart(order.slot, hash);
}

void finishLoad() {
    if (saveBusy()) return;
    const uintptr_t manager = saveManager();
    const int32_t result = game::callThiscall<int32_t>(game::kSaveManagerResultFunction, reinterpret_cast<void*>(manager));
    const std::optional<uint32_t> hash = save_blocks::hash(g_order->slot);
    switch (title_load_rule::afterLoad(*g_order, result, hash, g_tries, msSince(g_firstRequest))) {
        case Verdict::Commit: commit(*hash); return;
        case Verdict::Retry:
            if (result != 0) logger::write("title_load: load of slot %d failed (result %d), asking again", g_order->slot, result);
            else logger::write("title_load: slot %d of this copy is not the host's (hash %08x, host %08x), waiting for a newer copy",
                               g_order->slot, hash.value_or(0), g_order->expectedHash.value_or(0));
            enter(Stage::Retrying);
            return;
        case Verdict::Fail: fail(result != 0 ? "the load kept failing" : "this copy never became the host's"); return;
    }
}

void runStage() {
    switch (g_stage) {
        case Stage::FadingOut:
            if (game::fadeDone()) enter(Stage::Requesting);
            return;
        case Stage::Requesting: request(); return;
        case Stage::Loading: finishLoad(); return;
        case Stage::Retrying:
            if (msSince(g_stageSince) >= title_load_rule::kRetryMs) enter(Stage::Requesting);
            return;
        case Stage::Idle:
        case Stage::Started: return;
    }
}

bool menuSettled() {
    return game::fadeDone() && !saveBusy() &&
           game::callThiscall<bool>(game::kGuiSequenceFinishedFunction, reinterpret_cast<void*>(g_title), kWholeSequence);
}

// Host: the launcher's pick becomes this title's order the first time the main menu is ready for it.
void takeLauncherPick() {
    if (g_launcherPickDone || g_order) return;
    const std::optional<uint32_t> slotHash = g_launcherPick.active() ? save_blocks::hash(g_launcherPick.slot) : std::nullopt;
    switch (title_load_rule::launcherPick(g_launcherPick.active(), character_owner::linked(), character_owner::isHost(), g_launcherPick.hash,
                                          slotHash)) {
        case title_load_rule::LauncherPick::Wait: return;
        case title_load_rule::LauncherPick::Skip:
            g_launcherPickDone = true;
            if (g_launcherPick.active() && character_owner::isHost()) {
                logger::write("title_load: the launcher's slot %d changed since it was picked (hash %08x, picked %08x), not started",
                              g_launcherPick.slot, slotHash.value_or(0), g_launcherPick.hash.value_or(0));
                debug_overlay::toast("The session's save did not load.", kFailedToastSeconds, toast_queue::Style::Panel);
            }
            return;
        case title_load_rule::LauncherPick::Start:
            g_launcherPickDone = true;
            setOrder(Order{g_launcherPick.slot, true, std::nullopt});
            logger::write("title_load: the launcher's pick, slot %d, starts", g_launcherPick.slot);
            return;
    }
}

// Guest: the host's newest start becomes this title's order; an open Co-op page closes for it.
void followHost() {
    if (g_order) return;
    const std::optional<lobby_wire::SessionLoad> start = lobby::takeStart();
    if (!start) return;
    g_follows = true;
    setOrder(Order{start->slot, false, start->slotHash});
    session_slot::setOrder({start->slot, start->slotHash});
    logger::write("title_load: following the host's start %u of slot %d", start->serial, start->slot);
    native_page::requestClose();
}

}  // namespace

namespace title_load {

void setLauncherPick(const save_load_rule::Order& pick) { g_launcherPick = pick; }

void startHost(int32_t slot) {
    setOrder(Order{slot, true, std::nullopt});
    logger::write("title_load: host start of slot %d", slot);
}

void titleBefore(uintptr_t title, uint32_t state) {
    if (title != g_title) {
        g_title = title;
        if (g_stage != Stage::Idle) logger::write("title_load: a new title, the start is over");
        enter(Stage::Idle);
    }
    if (!g_order || !g_order->host) followHost();
    if (!g_order && g_stage == Stage::Idle && state == title_load_rule::kStateMainMenuInput && !native_page::active() && menuSettled()) {
        takeLauncherPick();
    }
    if (!g_order || g_stage == Stage::Started) return;
    if (native_page::active()) native_page::requestClose();
    if (g_stage == Stage::Idle) {
        if (!native_page::active() && title_load_rule::takesTitle(state, state, state == title_load_rule::kStateMainMenuInput && menuSettled())) {
            takeTitle(true);
        }
        return;
    }
    if (state != title_load_rule::kStateParked) {
        logger::write("title_load: the game moved the title (state 0x%x), slot %d not started", state, g_order->slot);
        clearOrder();
        enter(Stage::Idle);
        return;
    }
    runStage();
}

void titleAfter(uint32_t stateBefore, uint32_t stateAfter) {
    if (!g_order || g_stage != Stage::Idle || !title_load_rule::takesTitle(stateBefore, stateAfter, false)) return;
    takeTitle(false);
}

coop_menu_rule::StartStatus status() {
    if (g_stage == Stage::Retrying && g_order && !g_order->host) return coop_menu_rule::StartStatus::WaitingForSave;
    return g_order || g_stage == Stage::Started ? coop_menu_rule::StartStatus::Loading : coop_menu_rule::StartStatus::Idle;
}

bool busy() { return g_busy; }

bool followsHost() { return g_follows; }

}  // namespace title_load
