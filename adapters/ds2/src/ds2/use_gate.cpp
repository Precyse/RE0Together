// DEATH STRANDING 2: guest restriction. Before the player claims a use location (the "F" interactions), the player's
// DSPlayerUseLocationController asks the player entity MsgIsUseLocationClaimAllowed through the entity message
// dispatcher (0x1401618c0); any handler can refuse by setting the message's veto byte. The query does not name the
// use location, so the adapter looks at the controller's candidates itself: it refuses only while one of them is
// driven by a sequence network (order and quest triggers, not terminals: order_gate.cpp refuses the order
// transactions inside the terminal), and leaves other interactions alone. Sequence
// network use locations are learned from the game's own MsgSequenceNetworkUseLocationActivated / Deactivated, whose
// SequenceNetworkDSUseLocationInstance holds its use location while active (docs/DS2_NOTES.md, "Guest restrictions").
#include <windows.h>

#include <atomic>
#include <mutex>
#include <cstdio>
#include <set>
#include <string>

#include "decima/entity.h"
#include "decima/localized_text.h"
#include "decima/safe_read.h"
#include "ds2/player.h"
#include "ds2/remote_context.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "msvc_rtti.h"
#include "pattern_scan.h"

namespace {

// Entity message dispatch: (handler list at entity +0x2D0, entity lock at +0x2A8, message, flags).
constexpr const char* kDispatch =
    "48 89 54 24 10 57 48 83 EC 40 48 89 5C 24 50 48 8B FA 48 89 74 24 68 48 8B F1 4C 89 6C 24 30 45 8B E9 "
    "4C 89 7C 24 20 4D";
constexpr const char* kClaimQuery = "MsgIsUseLocationClaimAllowed";
constexpr const char* kActivated = "MsgSequenceNetworkUseLocationActivated";
constexpr const char* kDeactivated = "MsgSequenceNetworkUseLocationDeactivated";
constexpr const char* kController = "DSPlayerUseLocationController";
constexpr uintptr_t kEntityLock = 0x2A8;
constexpr uintptr_t kVeto = 0x10;  // MsgIsAllowedBase: byte, 0 when built (RTTI constructor 0x140831830), 1 = refused
constexpr uint8_t kRefused = 1;
constexpr uintptr_t kNodeInstance = 0x10;      // MsgSequenceNetworkNodeBase: the sending node instance
constexpr uintptr_t kInstanceLocation = 0x28;  // SequenceNetworkDSUseLocationInstance: its use location while active
// DSPlayerUseLocationController: the use locations it may claim (its update 0x140e82d00 walks them after the query).
constexpr uintptr_t kCandidateCount = 0x200, kCandidateData = 0x208;
constexpr size_t kCandidateStride = 0x20;  // first qword: the DSUseLocationGame
constexpr int32_t kMaxCandidates = 64;
constexpr uintptr_t kLocationResource = 0x40;  // DSUseLocationGame: its DSUseLocationResourceGame
constexpr uintptr_t kResourcePrompt = 0x90;    // ... whose LocalizedTextResource is the prompt ("Activate Terminal")

using DispatchFn = void (*)(uintptr_t handlers, uintptr_t lock, uintptr_t message, uint32_t flags);

struct Vtables {
    uintptr_t claimQuery = 0, activated = 0, deactivated = 0, controller = 0;
};

DispatchFn g_dispatch = nullptr;
Vtables g_vtables;
std::atomic<bool> g_blocking{false};
std::atomic<uintptr_t> g_lastRefused{0};  // the use location refused last (logged once per location)
std::mutex g_mutex;                       // guards g_sequenceNodes (the dispatcher runs on several game threads)
std::set<uintptr_t> g_sequenceNodes;      // SequenceNetworkDSUseLocationInstance objects that are active now

// Terminals are usable by the guest (only accepting and turning in orders is refused: order_gate.cpp). A terminal's
// use location is recognised by the UUID of its prompt text resource ("Activate Terminal"; the same in every language)
// or of its own resource, read at +0x10 of the resource (docs/DS2_NOTES.md, "Guest terminals").
constexpr const char* kTerminalTextUuid = "332c8d09007f44fbbddd64ee6d203e03";
constexpr const char* kTerminalResourceUuid = "25504a7813cd4a4eaba9a532390cb721";

std::string uuidText(uintptr_t object) {
    char text[33] = {};
    uint8_t bytes[16] = {};
    decima::safeCopy(bytes, object + 0x10, sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); ++i) sprintf_s(text + i * 2, 3, "%02x", bytes[i]);
    return text;
}

bool isTerminal(uintptr_t location) {
    const uintptr_t resource = decima::readPointer(location + kLocationResource);
    const uintptr_t text = decima::readPointer(resource + kResourcePrompt);
    return uuidText(text) == kTerminalTextUuid || uuidText(resource) == kTerminalResourceUuid;
}

// The first of the local player's claim candidates that belongs to an active sequence network node (other than a
// terminal), or 0.
uintptr_t sequenceLocationInReach(uintptr_t player) {
    const uintptr_t controller = decima::findComponent(player, g_vtables.controller);
    int32_t count = 0;
    const uintptr_t data = controller ? decima::readPointer(controller + kCandidateData) : 0;
    if (!data || !decima::safeRead(controller + kCandidateCount, count) || count <= 0 || count > kMaxCandidates) {
        return 0;
    }
    std::set<uintptr_t> locations;
    for (int32_t i = 0; i < count; ++i) locations.insert(decima::readPointer(data + i * kCandidateStride));
    std::lock_guard lock(g_mutex);
    for (uintptr_t node : g_sequenceNodes) {
        const uintptr_t location = decima::readPointer(node + kInstanceLocation);
        if (locations.contains(location) && !isTerminal(location)) return location;
    }
    return 0;
}

void dispatchDetour(uintptr_t handlers, uintptr_t lock, uintptr_t message, uint32_t flags) {
    const uintptr_t type = decima::readPointer(message);
    if (type == g_vtables.activated || type == g_vtables.deactivated) {
        const uintptr_t node = decima::readPointer(message + kNodeInstance);
        std::lock_guard guard(g_mutex);
        if (type == g_vtables.activated) {
            g_sequenceNodes.insert(node);
        } else {
            g_sequenceNodes.erase(node);
        }
    }
    const bool toRemote = remote_context::enter(handlers);
    g_dispatch(handlers, lock, message, flags);
    remote_context::leave(toRemote);
    if (!g_blocking.load() || type != g_vtables.claimQuery) return;
    const uintptr_t player = ds2::localPlayerEntity();
    if (lock - kEntityLock != player) return;
    const uintptr_t location = sequenceLocationInReach(player);
    if (!location) return;
    *reinterpret_cast<uint8_t*>(message + kVeto) = kRefused;
    if (g_lastRefused.exchange(location) == location) return;
    const uintptr_t resource = decima::readPointer(location + kLocationResource);
    logger::write("use_gate: refused \"%s\" (sequence network)",
                  decima::localizedText(decima::readPointer(resource + kResourcePrompt)).c_str());
}

bool install() {
    g_vtables = {msvc_rtti::vtableOf(kClaimQuery), msvc_rtti::vtableOf(kActivated), msvc_rtti::vtableOf(kDeactivated),
                 msvc_rtti::vtableOf(kController)};
    const uintptr_t dispatch = pattern_scan::find(kDispatch);
    if (!g_vtables.claimQuery || !g_vtables.activated || !g_vtables.deactivated || !g_vtables.controller || !dispatch) {
        logger::write("use_gate: dispatcher %p or a message/controller class not found",
                      reinterpret_cast<void*>(dispatch));
        return false;
    }
    return hooks::install("entity message dispatch", dispatch, reinterpret_cast<void*>(&dispatchDetour),
                          reinterpret_cast<void**>(&g_dispatch));
}

}  // namespace

namespace game {

bool watchInteractions() { return install(); }

void blockScriptedInteractions(bool block) {
    if (g_blocking.exchange(block) == block) return;
    logger::write("use_gate: sequence-network claims %s", block ? "refused (guest)" : "allowed");
}

}  // namespace game
