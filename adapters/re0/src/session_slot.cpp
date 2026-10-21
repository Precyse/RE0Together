#include "session_slot.h"

#include <atomic>
#include <chrono>
#include <cstring>

#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "protocol.h"
#include "room_phase.h"
#include "save_gate.h"
#include "save_redirect.h"
#include "session_rule.h"
#include "state_mode.h"

namespace {

using Clock = std::chrono::steady_clock;
using session_slot::kUnknown;

constexpr auto kAnnounceInterval = std::chrono::seconds(2);
constexpr float kRefusedToastSeconds = 3.0f;
constexpr int32_t kPlayerSlots = 20;  // slots 0..19 are player saves; higher ones are system data (options)
constexpr int32_t kCoopSlot = kPlayerSlots - 1;  // every save made during a session goes here, never over a solo save

bool isPlayerSlot(int32_t slot) { return slot >= 0 && slot < kPlayerSlots; }

using SaveRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot, int32_t arg);
using LoadRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot);

SaveRequestFn g_originalSave = nullptr;
SaveRequestFn g_originalSaveCopy = nullptr;
LoadRequestFn g_originalLoad = nullptr;

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

// A guest always loads the host's slot; the host remembers what it loads.
int32_t chooseSlot(int32_t requested) {
    const int32_t hostSlot = g_slot.load();
    if (guestInSession() && hostSlot != kUnknown && isPlayerSlot(requested)) {
        if (requested != hostSlot) logger::write("session_slot: load of slot %d turned into the host's slot %d", requested, hostSlot);
        return hostSlot;
    }
    return requested;
}

void remember(int32_t slot) {
    if (!isPlayerSlot(slot) || guestInSession() || g_slot.exchange(slot) == slot) return;
    logger::write("session_slot: playing slot %d", slot);
}

// The game's own data is current after a load or a save: floor changes before it need no replay for a joining guest.
void onSaveOrLoad(int32_t slot) {
    remember(slot);
    floor_items_sync::clearJournal();
}

bool __fastcall loadDetour(void* self, void* edx, int32_t slot) {
    const int32_t chosen = chooseSlot(slot);
    const bool accepted = g_originalLoad(self, edx, chosen);
    if (accepted) onSaveOrLoad(chosen);
    return accepted;
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
        debug_overlay::toast("Only the host can save", kRefusedToastSeconds);
        return false;
    }
    const bool coop = route == session_rule::SaveRoute::CoopSlot;
    if (coop && state_mode::on() && !partnerSettled()) return false;
    const int32_t target = coop ? kCoopSlot : slot;
    if (coop && slot != target) logger::write("session_slot: save to slot %d kept in the co-op slot %d", slot, target);
    const bool accepted = original(self, edx, target, arg);
    if (accepted) onSaveOrLoad(target);
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

bool guestSession() { return save_redirect::servingSession() && !character_owner::isHost(); }

bool hostInGame() { return room_phase::isGameplay(g_hostPhase.load()); }

bool enable() {
    return hooks::install("save request", game::kSaveRequestFunction, reinterpret_cast<void*>(&saveDetour),
                          reinterpret_cast<void**>(&g_originalSave)) &&
           hooks::install("save request (copy)", game::kSaveCopyRequestFunction, reinterpret_cast<void*>(&saveCopyDetour),
                          reinterpret_cast<void**>(&g_originalSaveCopy)) &&
           hooks::install("load request", game::kLoadRequestFunction, reinterpret_cast<void*>(&loadDetour),
                          reinterpret_cast<void**>(&g_originalLoad));
}

void uninstall() {
    hooks::remove(game::kSaveRequestFunction);
    hooks::remove(game::kSaveCopyRequestFunction);
    hooks::remove(game::kLoadRequestFunction);
}

}  // namespace session_slot
