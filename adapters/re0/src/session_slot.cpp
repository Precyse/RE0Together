#include "session_slot.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "flag_sync.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "inventory_sync.h"
#include "item_ledger.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "protocol.h"
#include "room_phase.h"
#include "save_gate.h"
#include "save_blocks.h"
#include "save_load_rule.h"
#include "save_redirect.h"
#include "session_rule.h"
#include "state_mode.h"
#include "world_epoch.h"

namespace {

using Clock = std::chrono::steady_clock;
using session_slot::kUnknown;

constexpr auto kAnnounceInterval = std::chrono::seconds(2);
constexpr float kRefusedToastSeconds = 3.0f;
constexpr int32_t kCoopSlot = save_load_rule::kPlayerSlots - 1;  // every save made during a session goes here, never over a solo save

using save_load_rule::isPlayerSlot;

using SaveRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot, int32_t arg);
using LoadRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot);

SaveRequestFn g_originalSave = nullptr;
SaveRequestFn g_originalSaveCopy = nullptr;
LoadRequestFn g_originalLoad = nullptr;
std::mutex g_orderMutex;
save_load_rule::Order g_loadOrder;  // guarded by g_orderMutex: the launcher's order, then the Co-op page's or the host's start

struct Announcement {
    int32_t slot;
    int32_t phase;
};
static_assert(sizeof(Announcement) == 8);

std::atomic<int32_t> g_slot{kUnknown};
std::atomic<int32_t> g_hostPhase{room_phase::kUnreadable};  // guest: the host's last announced room phase
Announcement g_announced{kUnknown, room_phase::kUnreadable};  // net thread only
Clock::time_point g_lastAnnounce;
save_gate_rule::Patience g_patience;  // game thread: how long the save screen's request has been refused

bool guestInSession() { return net_pad::active() && !character_owner::isHost(); }

// The slot a load request really loads: a guest the host's slot, and with a launcher order (docs/RE0_NOTES.md "Forced
// save load") every player's confirm loads the session's slot, the first time the launcher's pick if it is the bytes
// the launcher hashed.
int32_t chooseSlot(void* request, int32_t requested) {
    const save_load_rule::Order order = session_slot::order();
    const save_load_rule::Pick pick = save_load_rule::pick(order, guestInSession(), requested, g_slot.load());
    switch (pick.reason) {
        case save_load_rule::Reason::None: break;
        case save_load_rule::Reason::HostSlot:
            logger::write("session_slot: load of slot %d turned into the host's slot %d", requested, pick.slot);
            return pick.slot;
        case save_load_rule::Reason::SessionSlot:
            logger::write("save_load: slot %d requested, loading slot %d (redirect)", requested, pick.slot);
            return pick.slot;
        case save_load_rule::Reason::Order:
            if (!save_load_rule::hashAccepts(order, save_blocks::hash(pick.slot, reinterpret_cast<uintptr_t>(request)))) {
                logger::write("save_load: slot %d hash differs, not redirected", pick.slot);
                break;
            }
            logger::write("save_load: slot %d requested, loading slot %d (redirect)", requested, pick.slot);
            return pick.slot;
    }
    return requested;
}

void remember(int32_t slot) {
    if (!isPlayerSlot(slot) || guestInSession() || g_slot.exchange(slot) == slot) return;
    logger::write("session_slot: playing slot %d", slot);
}

// The game's own data is current after a save: floor changes before it need no replay for a joining guest.
void onSave(int32_t slot) {
    remember(slot);
    floor_items_sync::onSave();
}

// A load of a player save starts a new world: what was queued, proposed or requested for the old one must not reach it
// (docs/RE0_NOTES.md "State mode: sync review fixes"). The system data the game reads at boot is no world.
void onLoad(int32_t slot) {
    remember(slot);
    if (!isPlayerSlot(slot)) return;
    floor_items_sync::onLoad();
    inventory_sync::onLocalLoad();
    item_ledger::resetTake();
    flag_sync::onLocalLoad();
    world_epoch::onLocalLoad();
}

bool __fastcall loadDetour(void* self, void* edx, int32_t slot) {
    const int32_t chosen = chooseSlot(self, slot);
    const bool accepted = g_originalLoad(self, edx, chosen);
    if (accepted) onLoad(chosen);
    return accepted;
}

// The load request's prologue is checked first: a game build that moved it must not get a hook on other code.
bool installLoadHook() {
    if (!game::bytesMatch(game::kLoadRequestFunction, game::kLoadRequestBytes)) {
        logger::write("session_slot: load request 0x%x does not hold the expected bytes, not hooked",
                      static_cast<unsigned>(game::kLoadRequestFunction));
        return false;
    }
    return hooks::install("load request", game::kLoadRequestFunction, reinterpret_cast<void*>(&loadDetour),
                          reinterpret_cast<void**>(&g_originalLoad));
}

// State mode: the host's save waits until the partner stands in plain gameplay, for at most save_gate_rule::kMaxWaitMs
// (the save screen has no other way out of a refused request).
bool partnerSettled() {
    const save_gate_rule::Verdict verdict = save_gate::check();
    switch (g_patience.decide(verdict, pose_wire::clockMs())) {
        case save_gate_rule::Decision::Save: return true;
        case save_gate_rule::Decision::SaveUnsettled:
            logger::write("session_slot: saved without the partner settled (%s)", save_gate_rule::name(verdict));
            return true;
        case save_gate_rule::Decision::Wait: break;
    }
    debug_stats::count(debug_stats::Counter::SaveWaits);
    logger::writeUnlessRepeated("session_slot: save waits for plain gameplay (%s)", save_gate_rule::name(verdict));
    return false;
}

// Only the in-game save screen is a player's save; the game also saves the system data (slot 0x15) on its own.
// A refused request is retried by the save screen every frame, so the refusal log line is written once per episode.
bool routeSave(SaveRequestFn original, void* self, void* edx, int32_t slot, int32_t arg) {
    const auto route = session_rule::saveRoute(isPlayerSlot(slot), game_state::roomPhase(), session_slot::guestSession(),
                                               net_pad::active());
    if (route == session_rule::SaveRoute::Refuse) {
        logger::writeUnlessRepeated("session_slot: guest save of slot %d refused", slot);
        debug_overlay::toast("Only the host can save the game.", kRefusedToastSeconds, toast_queue::Style::Panel);
        return false;
    }
    const bool coop = route == session_rule::SaveRoute::CoopSlot;
    if (coop && state_mode::on() && !partnerSettled()) return false;
    const int32_t target = coop ? kCoopSlot : slot;
    if (coop && slot != target) logger::write("session_slot: save to slot %d kept in the co-op slot %d", slot, target);
    const bool accepted = original(self, edx, target, arg);
    if (accepted) onSave(target);
    return accepted;
}

bool __fastcall saveDetour(void* self, void* edx, int32_t slot, int32_t arg) {
    return routeSave(g_originalSave, self, edx, slot, arg);
}

bool __fastcall saveCopyDetour(void* self, void* edx, int32_t slot, int32_t arg) {
    return routeSave(g_originalSaveCopy, self, edx, slot, arg);
}

}  // namespace

namespace session_slot {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgSaveSlot || frame.payload.size() != sizeof(Announcement) ||
        !character_owner::isHostSlot(frame.slot) || character_owner::isHost()) {
        return;
    }
    Announcement announcement;
    std::memcpy(&announcement, frame.payload.data(), sizeof(announcement));
    g_hostPhase = announcement.phase;
    if (g_slot.exchange(announcement.slot) != announcement.slot) logger::write("session_slot: host plays slot %d", announcement.slot);
}

void onNetTick(NetClient& net) {
    const Announcement now{g_slot.load(), game_state::roomPhase()};
    if (!net_pad::active() || !character_owner::isHost() || now.slot == kUnknown) return;
    const auto time = Clock::now();
    const bool changed = now.slot != g_announced.slot || now.phase != g_announced.phase;
    if (!changed && time - g_lastAnnounce < kAnnounceInterval) return;
    if (!net.send(proto::kMsgSaveSlot, true, proto::kSlotAll, proto::bytesOf(now))) return;
    g_announced = now;
    g_lastAnnounce = time;
}

int32_t current() { return g_slot.load(); }

save_load_rule::Order order() {
    std::lock_guard lock(g_orderMutex);
    return g_loadOrder;
}

void setOrder(const save_load_rule::Order& next) {
    {
        std::lock_guard lock(g_orderMutex);
        g_loadOrder = next;
    }
    if (!next.active()) {
        logger::write("save_load: no slot picked, the load list decides");
        return;
    }
    g_slot = next.slot;  // a pick replaces what earlier loads and saves left: later loads return to it
    logger::write("save_load: the session plays slot %d", next.slot);
}

bool guestSession() { return save_redirect::servingSession() && !character_owner::isHost(); }

bool hostInGame() { return room_phase::isGameplay(g_hostPhase.load()); }

bool enable(const save_load_rule::Order& loadOrder) {
    {
        std::lock_guard lock(g_orderMutex);
        g_loadOrder = loadOrder;
    }
    return hooks::install("save request", game::kSaveRequestFunction, reinterpret_cast<void*>(&saveDetour),
                          reinterpret_cast<void**>(&g_originalSave)) &&
           hooks::install("save request (copy)", game::kSaveCopyRequestFunction, reinterpret_cast<void*>(&saveCopyDetour),
                          reinterpret_cast<void**>(&g_originalSaveCopy)) &&
           installLoadHook();
}

void uninstall() {
    hooks::remove(game::kSaveRequestFunction);
    hooks::remove(game::kSaveCopyRequestFunction);
    hooks::remove(game::kLoadRequestFunction);
}

}  // namespace session_slot
