#include "session_slot.h"

#include <atomic>
#include <chrono>
#include <cstring>

#include "character_owner.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"

namespace {

using Clock = std::chrono::steady_clock;
using session_slot::kUnknown;

constexpr auto kAnnounceInterval = std::chrono::seconds(2);
constexpr int32_t kPlayerSlots = 20;  // slots 0..19 are player saves; higher ones are system data (options)

bool isPlayerSlot(int32_t slot) { return slot >= 0 && slot < kPlayerSlots; }

using LoadRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot, int32_t arg);
using SaveRequestFn = bool(__fastcall*)(void* self, void* edx, int32_t slot);

LoadRequestFn g_originalLoad = nullptr;
LoadRequestFn g_originalLoadAlt = nullptr;
SaveRequestFn g_originalSave = nullptr;

std::atomic<int32_t> g_slot{kUnknown};
int32_t g_announced = kUnknown;  // net thread only
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

bool __fastcall saveDetour(void* self, void* edx, int32_t slot) {
    const bool accepted = g_originalSave(self, edx, slot);
    if (accepted) remember(slot);
    return accepted;
}

}  // namespace

namespace session_slot {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgSaveSlot || frame.payload.size() != sizeof(int32_t) ||
        !character_owner::isHostSlot(frame.slot) || character_owner::isHost()) {
        return;
    }
    int32_t slot = kUnknown;
    std::memcpy(&slot, frame.payload.data(), sizeof(slot));
    if (g_slot.exchange(slot) != slot) logger::write("session_slot: host plays slot %d", slot);
}

void onNetTick(NetClient& net) {
    const int32_t slot = g_slot.load();
    if (!net_pad::active() || !character_owner::isHost() || slot == kUnknown) return;
    const auto now = Clock::now();
    if (slot == g_announced && now - g_lastAnnounce < kAnnounceInterval) return;
    if (!net.send(proto::kMsgSaveSlot, true, proto::kSlotAll, {reinterpret_cast<const uint8_t*>(&slot), sizeof(slot)})) return;
    g_announced = slot;
    g_lastAnnounce = now;
}

int32_t current() { return g_slot.load(); }

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
