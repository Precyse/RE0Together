#include "equipment.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <optional>
#include <vector>

#include "build_gate.h"
#include "cd_sites.h"
#include "hooks.h"
#include "link.h"
#include "log.h"
#include "presence_recv.h"
#include "presence_rule.h"
#include "presence_send.h"
#include "safe_read.h"
#include "template_rule.h"
#include "troc_catalogue.h"
#include "wire_cd.h"

namespace {

// Forwarded with eight integer arguments, as the funnels are: the writer's own plus caller stack slots it ignores.
using Writer = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

constexpr uint32_t kServerActorKey = 0x60;  // u32 key of a server actor (the writer's first argument)
constexpr uint32_t kListCount = 0x08;       // {items*, u32 count}
constexpr uint32_t kItemStride = 0x08;      // {u32 item, u16 slot, pad}
constexpr uint32_t kItemSlot = 0x04;
constexpr size_t kPendingCalls = 16;        // writer calls between two main ticks; more are dropped and counted
constexpr int kLockSpins = 1000;            // the server thread never waits longer for the main tick's copy

struct WriterCall {
    uint32_t serial = 0;
    uint32_t count = 0;
    std::array<equip_rule::Item, equip_rule::kMaxWriterItems> items{};
};

Writer g_original = nullptr;
std::atomic<uint32_t> g_ownKey{0};
SRWLOCK g_lock = SRWLOCK_INIT;
std::array<WriterCall, kPendingCalls> g_pending;  // under g_lock; fixed storage, the hook must not allocate
size_t g_pendingCount = 0;
std::atomic<uint32_t> g_writerDropped{0};

struct Opcodes {
    uint16_t unequipItem = 0, otherEquip = 0, otherUnequip = 0;
} g_op;

// Sender.
equip_rule::Loadout g_loadout;
uint16_t g_loadoutGeneration = 0;
bool g_dirty = false;

// Receiver: per peer, its newest CHAR_EQUIP and what this machine put on its copy.
struct Peer {
    std::optional<wire_cd::Equip> wanted;
    uint32_t appliedSpawn = 0;
    std::map<uint16_t, equip_rule::CopySlot> copy;
    std::optional<uint32_t> lastSerial;
    bool pending = false;  // `wanted` not yet applied to the current spawn
};
std::map<uint8_t, Peer> g_peers;
equipment::Stats g_stats;

bool readCall(uint64_t actor, uint64_t serialPtr, uint64_t list, WriterCall& out) {
    uint32_t key = 0;
    uintptr_t items = 0;
    if (!safe_read::value(actor + kServerActorKey, key) || key != g_ownKey.load(std::memory_order_relaxed)) return false;
    if (!safe_read::value(serialPtr, out.serial) || !safe_read::value(list, items) ||
        !safe_read::value(list + kListCount, out.count) || out.count == 0 || out.count > out.items.size()) {
        return false;
    }
    for (uint32_t i = 0; i < out.count; ++i) {
        const uintptr_t at = items + i * kItemStride;
        if (!safe_read::value(at, out.items[i].item) || !safe_read::value(at + kItemSlot, out.items[i].slot)) return false;
    }
    return true;
}

void queueCall(const WriterCall& call) {
    for (int spin = 0; !TryAcquireSRWLockExclusive(&g_lock); ++spin) {
        if (spin > kLockSpins) {
            g_writerDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        YieldProcessor();
    }
    if (g_pendingCount < g_pending.size()) {
        g_pending[g_pendingCount++] = call;
    } else {
        g_writerDropped.fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

// Server thread. Reads the arguments only; the original always runs.
uint64_t __fastcall writerDetour(uint64_t actor, uint64_t a2, uint64_t serialPtr, uint64_t list, uint64_t a5, uint64_t a6,
                                 uint64_t a7, uint64_t a8) {
    if (g_ownKey.load(std::memory_order_relaxed)) {
        WriterCall call;
        if (readCall(actor, serialPtr, list, call)) queueCall(call);
    }
    return g_original(actor, a2, serialPtr, list, a5, a6, a7, a8);
}

std::vector<WriterCall> takeCalls() {
    AcquireSRWLockExclusive(&g_lock);
    std::vector<WriterCall> out(g_pending.begin(), g_pending.begin() + g_pendingCount);
    g_pendingCount = 0;
    ReleaseSRWLockExclusive(&g_lock);
    return out;
}

void note(uint32_t serial, std::span<const equip_rule::Item> items) {
    if (g_loadout.apply(serial, items)) g_dirty = true;
}

void sendLoadout() {
    std::vector<wire_cd::EquipEntry> entries;
    for (const equip_rule::SlotState& s : g_loadout.slots()) {
        entries.push_back({s.slot, s.item, static_cast<uint8_t>(s.hadAtLoad)});
    }
    if (link::send(wire_cd::kCharEquip, true, wire_cd::encodeEquip(g_loadoutGeneration, g_loadout.serial(), entries))) {
        ++g_stats.sent;
        g_dirty = false;
    }
}

void tickSender(bool peerJoined) {
    const uint32_t own = presence_send::ownPlayerKey();
    g_ownKey.store(own, std::memory_order_relaxed);
    // Calls queued so far belong to the template they were made under: in before a new template clears the loadout.
    for (const WriterCall& call : takeCalls()) note(call.serial, std::span(call.items.data(), call.count));
    const uint16_t generation = presence_send::generation();
    if (generation != g_loadoutGeneration) {  // a new template already shows the current gear
        g_loadout.clear();
        g_loadoutGeneration = generation;
        g_dirty = true;
    }
    if (generation && (g_dirty || peerJoined)) sendLoadout();
}

void inject(uint16_t opcode, std::span<const uint8_t> payload) { funnels::inject(presence_rule::packetOf(opcode, payload)); }

void applyToCopy(uint8_t slot, Peer& peer, const presence_recv::CopyView& view) {
    if (view.spawnCount != peer.appliedSpawn) {  // a fresh copy wears the template's gear again
        peer.appliedSpawn = view.spawnCount;
        peer.copy.clear();
        peer.pending = true;
    }
    if (!peer.pending || !peer.wanted || peer.wanted->header.generation != view.generation) return;
    std::vector<equip_rule::SlotState> wanted;
    for (const wire_cd::EquipEntry& e : peer.wanted->entries) wanted.push_back({e.slot, e.item, e.hadAtLoad != 0});
    const equip_rule::Plan plan = equip_rule::plan(wanted, peer.copy);
    peer.pending = false;
    if (plan.empty()) return;
    const uint32_t key = template_rule::copyKeys(slot).player;
    const uint32_t senderSerial = peer.wanted->header.serial;
    for (uint16_t unequip : plan.unequips) {
        peer.lastSerial = equip_rule::nextSerial(peer.lastSerial, senderSerial);
        inject(g_op.otherUnequip, equip_rule::otherUnequip(key, unequip, *peer.lastSerial));
    }
    if (!plan.puts.empty()) {
        peer.lastSerial = equip_rule::nextSerial(peer.lastSerial, senderSerial);
        inject(g_op.otherEquip, equip_rule::otherEquip(key, *peer.lastSerial, plan.puts));
    }
    ++g_stats.applied;
    logger::write("equipment: slot %u copy: %zu unequip(s), %zu put(s), serial %u", slot, plan.unequips.size(),
                  plan.puts.size(), *peer.lastSerial);
}

void tickReceiver(uint64_t nowMs, std::span<const uint8_t> slotsPresent) {
    for (auto it = g_peers.begin(); it != g_peers.end();) {
        if (std::find(slotsPresent.begin(), slotsPresent.end(), it->first) == slotsPresent.end()) {
            it = g_peers.erase(it);
            continue;
        }
        const auto view = presence_recv::copyView(it->first, nowMs);
        if (view && view->live && presence_recv::canInject()) applyToCopy(it->first, it->second, *view);
        ++it;
    }
}

}  // namespace

namespace equipment {

bool resolveOpcodes() {
    const troc_table::Table& table = troc_catalogue::table();
    const auto unequipItem = table.opcodeOf("UnequipItemAck"), otherEquip = table.opcodeOf("OtherEquipItemAck"),
               otherUnequip = table.opcodeOf("OtherUnequipItemAck");
    if (!unequipItem || !otherEquip || !otherUnequip) return false;
    g_op = {*unequipItem, *otherEquip, *otherUnequip};
    funnels::watch(g_op.unequipItem, true);
    return true;
}

bool install() {
    if (!build_gate::setIntact(cd_sites::kEquipment)) {
        logger::write("equipment: not patched (site bytes differ)");
        return false;
    }
    const bool ok = hooks::install(cd_sites::kEquipWriter.name, cd_sites::kEquipWriter.address,
                                   reinterpret_cast<void*>(&writerDetour), reinterpret_cast<void**>(&g_original));
    logger::write(ok ? "equipment: 1 patched" : "equipment: not patched (hook install failed)");
    return ok;
}

void onEvent(const funnels::Event& event) {
    if (event.direction != packet::Direction::ServerToClient || event.header.opcode != g_op.unequipItem) return;
    const auto unequip = equip_rule::parseUnequipItemAck(event.payload);
    if (unequip && unequip->key && unequip->key == g_ownKey.load(std::memory_order_relaxed)) {
        note(unequip->serial, std::span(&unequip->item, 1));
    }
}

void tick(uint64_t nowMs, bool peerJoined, std::span<const uint8_t> slotsPresent) {
    tickSender(peerJoined);
    tickReceiver(nowMs, slotsPresent);
}

bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload) {
    if (type != wire_cd::kCharEquip) return false;
    auto equip = wire_cd::decodeEquip(payload);
    if (!equip) return false;
    Peer& peer = g_peers[slot];
    peer.wanted = std::move(*equip);
    peer.pending = true;
    return true;
}

Stats stats() {
    Stats out = g_stats;
    out.writerDropped = g_writerDropped.load(std::memory_order_relaxed);
    return out;
}

}  // namespace equipment
