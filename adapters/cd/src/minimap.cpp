#include "minimap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <optional>

#include "build_gate.h"
#include "cd_sites.h"
#include "funnels.h"
#include "hooks.h"
#include "link.h"
#include "log.h"
#include "pin_rule.h"
#include "presence_recv.h"
#include "presence_rule.h"
#include "presence_send.h"
#include "safe_read.h"
#include "template_rule.h"
#include "troc_catalogue.h"
#include "world_gate.h"

namespace {

using IconDecide = void(__fastcall*)(void* component, uint16_t* icon, uint16_t* subIcon, uint8_t* a, uint8_t* b);

constexpr uint32_t kComponentActor = 0x08;  // ClientMinimapActorComponent -> actor
constexpr uint32_t kActorKey = 0x60;
constexpr uint32_t kTableHolder = 0x08;
constexpr uint32_t kTableKeys = 0x270;
constexpr uint32_t kOutlinedArrow = 0x124;   // icon key 23, the co-op default (0x126 is the local player's own arrow)
constexpr uint16_t kNoIcon = 0xFFFF;

IconDecide g_original = nullptr;
std::atomic<uint32_t> g_iconsGiven{0};

struct Opcodes {
    uint16_t createPin = 0, removePin = 0;
} g_op;

struct PeerPin {
    pin_rule::Pin pin;
    uint32_t owner = 0;  // the local player key the pin was created under
};
std::map<uint8_t, PeerPin> g_pins;
uint32_t g_pinsCreated = 0;

uint16_t iconFromTable(uint32_t offset) {
    for (uintptr_t root : {cd_sites::kMinimapIconRoot, cd_sites::kMinimapIconRootAlt}) {
        uintptr_t p = safe_read::pointer(root);
        p = p ? safe_read::pointer(p) : 0;
        p = p ? safe_read::pointer(p + kTableHolder) : 0;
        p = p ? safe_read::pointer(p + kTableKeys) : 0;
        uint16_t key = kNoIcon;
        if (p && safe_read::value(p + offset, key)) return key;
    }
    return kNoIcon;
}

// The minimap's thread. Copies get the arrow; everyone else the game's own answer.
void __fastcall iconDetour(void* component, uint16_t* icon, uint16_t* subIcon, uint8_t* a, uint8_t* b) {
    const uintptr_t actor = safe_read::pointer(reinterpret_cast<uintptr_t>(component) + kComponentActor);
    uint32_t key = 0;
    if (actor && safe_read::value(actor + kActorKey, key) && template_rule::copySlotOfPlayer(key)) {
        const uint16_t arrow = iconFromTable(kOutlinedArrow);
        if (arrow != kNoIcon) {
            *icon = arrow;
            *subIcon = kNoIcon;
            *a = 0;
            *b = 0;
            g_iconsGiven.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    g_original(component, icon, subIcon, a, b);
}

void inject(uint16_t opcode, std::span<const uint8_t> payload) { funnels::inject(presence_rule::packetOf(opcode, payload)); }

// Where the peer's pin belongs now, or null when it should not be shown.
std::optional<std::array<float, 3>> pinPlace(uint8_t slot, uint64_t nowMs) {
    if (presence_recv::copyView(slot, nowMs)) return std::nullopt;  // the copy itself is here
    const auto world = link::peerWorld(slot);
    if (!world || world->gateState != static_cast<uint8_t>(world_gate_rule::State::Ready)) return std::nullopt;
    const uint32_t ownField = world_gate::report().fieldKey;
    if (ownField && world->fieldKey && ownField != world->fieldKey) return std::nullopt;
    return std::array<float, 3>{world->position[0], world->position[1], world->position[2]};
}

void tickPin(uint8_t slot, PeerPin& peer, uint64_t nowMs, bool canShow) {
    if (!canShow) {
        peer.pin.shown = false;  // our own load clears injected pins with the rest of the map state
        return;
    }
    const uint32_t owner = presence_send::ownPlayerKey();
    const pin_rule::Step step = pin_rule::decide(peer.pin, pinPlace(slot, nowMs), nowMs);
    if (step.remove) inject(g_op.removePin, pin_rule::removePayload(peer.owner, pin_rule::pinId(slot)));
    if (step.create) {
        peer.owner = owner;
        inject(g_op.createPin, pin_rule::createPayload(owner, pin_rule::pinId(slot), peer.pin.at));
        ++g_pinsCreated;
    }
}

}  // namespace

namespace minimap {

bool resolveOpcodes() {
    const troc_table::Table& table = troc_catalogue::table();
    const auto create = table.opcodeOf("CreateOrChangePinMarkerAck"), remove = table.opcodeOf("RemovePinMarkerAck");
    if (!create || !remove) return false;
    g_op = {*create, *remove};
    return true;
}

bool install() {
    if (!build_gate::setIntact(cd_sites::kMinimap)) {
        logger::write("minimap: not patched (site bytes differ)");
        return false;
    }
    const bool ok = hooks::install(cd_sites::kMinimapIcon.name, cd_sites::kMinimapIcon.address,
                                   reinterpret_cast<void*>(&iconDetour), reinterpret_cast<void**>(&g_original));
    logger::write(ok ? "minimap: 1 patched" : "minimap: not patched (hook install failed)");
    return ok;
}

void tick(uint64_t nowMs, std::span<const uint8_t> slotsPresent) {
    const bool canShow = g_op.createPin && funnels::injectionOn() && presence_send::ownPlayerKey() &&
                         world_gate::state() == world_gate_rule::State::Ready;
    for (auto it = g_pins.begin(); it != g_pins.end();) {
        if (std::find(slotsPresent.begin(), slotsPresent.end(), it->first) == slotsPresent.end()) {
            if (canShow && it->second.pin.shown) inject(g_op.removePin, pin_rule::removePayload(it->second.owner, pin_rule::pinId(it->first)));
            it = g_pins.erase(it);
            continue;
        }
        ++it;
    }
    for (uint8_t slot : slotsPresent) tickPin(slot, g_pins[slot], nowMs, canShow);
}

Stats stats() { return {g_iconsGiven.load(std::memory_order_relaxed), g_pinsCreated}; }

}  // namespace minimap
