#include "session_slot.h"

#include <atomic>
#include <chrono>
#include <cstring>

#include "character_owner.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "room_phase.h"

namespace {

using Clock = std::chrono::steady_clock;
using session_slot::kUnknown;

constexpr auto kAnnounceInterval = std::chrono::seconds(2);
constexpr int32_t kPlayerSlots = 20;  // slots 0..19 are player saves; higher ones are system data (options)
constexpr int32_t kCoopSlot = kPlayerSlots - 1;  // every save made during a session goes here, never over a solo save

bool isPlayerSlot(int32_t slot) { return slot >= 0 && slot < kPlayerSlots; }

using LoadRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot, int32_t arg);
using SaveRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot);

LoadRequestFn g_originalLoad = nullptr;
LoadRequestFn g_originalLoadAlt = nullptr;
SaveRequestFn g_originalSave = nullptr;

struct Announcement {
    int32_t slot;
    int32_t phase;
};
static_assert(sizeof(Announcement) == 8);

std::atomic<int32_t> g_slot{kUnknown};
std::atomic<int32_t> g_hostPhase{room_phase::kUnreadable};  // guest: the host's last announced room phase
Announcement g_announced{kUnknown, room_phase::kUnreadable};  // net thread only
Clock::time_point g_lastAnnounce;

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

bool __fastcall loadDetour(void* self, void* edx, int32_t slot, int32_t arg) {
    const int32_t chosen = chooseSlot(slot);
    const bool accepted = g_originalLoad(self, edx, chosen, arg);
    if (accepted) remember(chosen);
    return accepted;
}

bool __fastcall loadAltDetour(void* self, void* edx, int32_t slot, int32_t arg) {
    const int32_t chosen = chooseSlot(slot);
    const bool accepted = g_originalLoadAlt(self, edx, chosen, arg);
    if (accepted) remember(chosen);
    return accepted;
}

// Only the in-game save screen is a player's save; the game also saves on its own at boot.
bool __fastcall saveDetour(void* self, void* edx, int32_t slot) {
    const bool coop = net_pad::active() && isPlayerSlot(slot) && game_state::roomPhase() == room_phase::Save;
    const int32_t target = coop ? kCoopSlot : slot;
    if (coop && slot != target) logger::write("session_slot: save to slot %d kept in the co-op slot %d", slot, target);
    const bool accepted = g_originalSave(self, edx, target);
    if (accepted) remember(target);
    return accepted;
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

bool hostInGame() { return room_phase::isGameplay(g_hostPhase.load()); }

bool enable() {
    return hooks::install("save load request", game::kSaveLoadRequestFunction, reinterpret_cast<void*>(&loadDetour),
                          reinterpret_cast<void**>(&g_originalLoad)) &&
           hooks::install("save load request (alt)", game::kSaveLoadAltRequestFunction,
                          reinterpret_cast<void*>(&loadAltDetour), reinterpret_cast<void**>(&g_originalLoadAlt)) &&
           hooks::install("save request", game::kSaveRequestFunction, reinterpret_cast<void*>(&saveDetour),
                          reinterpret_cast<void**>(&g_originalSave));
}

void uninstall() {
    hooks::remove(game::kSaveLoadRequestFunction);
    hooks::remove(game::kSaveLoadAltRequestFunction);
    hooks::remove(game::kSaveRequestFunction);
}

}  // namespace session_slot
