#include "gear_restore.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

#include "ds2/sim_tick.h"
#include "equip_sync.h"
#include "game.h"
#include "gear_snapshot.h"
#include "log.h"
#include "paths.h"

namespace {

using Clock = std::chrono::steady_clock;
using gear_snapshot::Item;
using gear_snapshot::Items;

constexpr wchar_t kFileName[] = L"\\personal_gear.txt";
constexpr wchar_t kTempSuffix[] = L".tmp";
constexpr auto kRequestInterval = std::chrono::milliseconds(500);
constexpr int kMaxRequestsPerRound = 4;
// After the restore starts, the game needs time to serve the creations: nothing is written until they show in Sam's slots.
constexpr auto kRestoreWindow = std::chrono::seconds(15);
constexpr auto kSnapshotInterval = std::chrono::seconds(30);
constexpr uint64_t kLocalPlayerOwner = 0;

enum class Phase { Waiting, Restoring, Watching };

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_guest{false};

// Simulation thread only.
Phase g_phase = Phase::Waiting;
uint32_t g_epoch = 0;
Items g_baseline;  // what Sam held when this join settled (the host's save gear, before any restore)
Items g_toAdd;     // still to give back; a piece leaves the list when its request is made, never asked twice
Items g_lastWritten;
Clock::time_point g_phaseStart, g_lastRequest, g_lastWrite;

std::wstring filePath() { return coopDirectory() + kFileName; }

Items readHeld() {
    Items items;
    for (const game::Cargo& piece : game::carriedCargo()) {
        if (!piece.orderId && !piece.secondId) {
            items.push_back({gear_snapshot::kBackpackSlot, piece.type, piece.category, piece.durability});
        }
    }
    for (const game::SlotPiece& piece : game::slotPiecesOfKinds(kLocalPlayerOwner, equip_sync::kMirroredSlots,
                                                                std::size(equip_sync::kMirroredSlots))) {
        items.push_back({piece.slot, piece.type, 0, 0});
    }
    return items;
}

Items readSnapshot() {
    Items items;
    std::ifstream file(filePath(), std::ios::binary);
    if (!file) return items;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!gear_snapshot::parse(text, items)) {
        logger::write("gear_restore: personal_gear.txt is not a snapshot of this version, ignored");
    }
    return items;
}

void writeSnapshot(const Items& items) {
    const std::wstring path = filePath();
    const std::wstring temp = path + kTempSuffix;
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << gear_snapshot::format(items);
        if (!file.good()) {
            logger::write("gear_restore: could not write the snapshot");
            return;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        logger::write("gear_restore: could not replace the snapshot");
    }
}

// False when the game asked to try this piece again later.
bool request(const Item& item) {
    if (item.slot == gear_snapshot::kBackpackSlot) {
        game::Cargo piece;
        piece.type = item.type;
        piece.category = item.category;
        piece.durability = item.durability;
        const game::AddResult result = game::addCargo(piece);
        if (result == game::AddResult::Retry) return false;
        logger::write("gear_restore: backpack piece %u %s", item.type, result == game::AddResult::Done ? "requested" : "refused");
        return true;
    }
    const bool ok = game::addSlotPiece(kLocalPlayerOwner, item.slot, item.type);
    logger::write("gear_restore: slot %u piece %u %s", item.slot, item.type, ok ? "requested" : "refused");
    return true;
}

void begin(Clock::time_point now) {
    g_baseline = readHeld();
    g_toAdd = gear_snapshot::minus(readSnapshot(), g_baseline);
    g_phase = Phase::Restoring;
    g_phaseStart = now;
    g_epoch = sim_tick::gameplayEpoch();
    logger::write("gear_restore: join settled, Sam holds %zu pieces, %zu to give back", g_baseline.size(), g_toAdd.size());
}

void restoreRound(Clock::time_point now) {
    if (now - g_lastRequest < kRequestInterval) return;
    g_lastRequest = now;
    for (int sent = 0; sent < kMaxRequestsPerRound && !g_toAdd.empty(); ++sent) {
        if (!request(g_toAdd.front())) return;
        g_toAdd.erase(g_toAdd.begin());
    }
}

void watch(Clock::time_point now) {
    if (now - g_lastWrite < kSnapshotInterval) return;
    g_lastWrite = now;
    const Items gained = gear_snapshot::minus(readHeld(), g_baseline);
    if (gear_snapshot::format(gained) == gear_snapshot::format(g_lastWritten)) return;
    writeSnapshot(gained);
    g_lastWritten = gained;
    logger::write("gear_restore: snapshot written, %zu pieces gained since the join", gained.size());
}

void simTick() {
    if (!g_enabled.load()) return;
    const auto now = Clock::now();
    if (!g_guest.load() || !sim_tick::worldReady() || !game::gameplaySettled()) {
        g_phase = Phase::Waiting;
        return;
    }
    if (g_phase == Phase::Waiting || g_epoch != sim_tick::gameplayEpoch()) begin(now);
    if (g_phase == Phase::Restoring) {
        restoreRound(now);
        if (now - g_phaseStart > kRestoreWindow) {
            if (!g_toAdd.empty()) logger::write("gear_restore: %zu pieces could not be given back", g_toAdd.size());
            g_phase = Phase::Watching;
            g_lastWrite = now;
        }
        return;
    }
    watch(now);
}

}  // namespace

namespace gear_restore {

void setEnabled(bool enabled) { g_enabled = enabled; }

void installEarly() { sim_tick::add(&simTick, "gear restore", sim_tick::Gate::Gameplay); }

void tick(const SessionSnapshot& session) { g_guest = session.linked && session.localSlot != session.hostSlot; }

}  // namespace gear_restore
