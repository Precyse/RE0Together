// DEATH STRANDING 2: guest orders. Accepting an order at a terminal queues the menu command
// DSGameMenuCommandReservedStartMission: its execute 0x1413f0520 prepares each reserved order's cargo on the terminal's shelf
// and its commit 0x1413effe0 queues the starts. While the local player is a guest neither runs; the commit sends the
// reserved orders to the host as OrderRequest (story.cpp), which starts them in its world and gives the guest their cargo
// (order_handoff). Cancelling needs no change: nothing was prepared. A guest's turn-in runs normally (story.cpp forwards
// its success request to the host; docs/DS2_NOTES.md, "Guest orders").
#include <atomic>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kStartCommandExecute = 0x1413f0520;  // (command, menu context)
constexpr uintptr_t kStartCommit = 0x1413effe0;          // (controller)
constexpr uintptr_t kReservedList = 0x30;                // controller: the block the terminal menu filled
constexpr uintptr_t kReservedCount = 0x280;              // block: int32 count; the entries start at the block
constexpr size_t kReservedEntrySize = 16;                // {u64 mission id, u32 row, i32 second argument}
constexpr uintptr_t kEntryRow = 8, kEntryExtra = 12;
constexpr int32_t kMaxReserved = 40;

using ExecuteFn = void (*)(uintptr_t command, uintptr_t context);
using CommitFn = void (*)(uintptr_t controller);

std::atomic<bool> g_blocking{false};
ExecuteFn g_execute = nullptr;
CommitFn g_commit = nullptr;

void executeDetour(uintptr_t command, uintptr_t context) {
    if (!g_blocking.load()) g_execute(command, context);
}

void commitDetour(uintptr_t controller) {
    if (!g_blocking.load()) {
        g_commit(controller);
        return;
    }
    const uintptr_t block = decima::readPointer(controller + kReservedList);
    int32_t count = 0;
    if (!block || !decima::safeRead(block + kReservedCount, count)) return;
    for (int32_t i = 0; i < count && i < kMaxReserved; ++i) {
        const uintptr_t entry = block + i * kReservedEntrySize;
        uint64_t missionId = 0;
        uint32_t row = 0;
        int32_t extra = 0;
        if (!decima::safeRead(entry, missionId) || !decima::safeRead(entry + kEntryRow, row) ||
            !decima::safeRead(entry + kEntryExtra, extra)) {
            continue;
        }
        game::forwardOrderRequest(missionId, row, extra);
        logger::write("order_gate: order %llx sent to the host", static_cast<unsigned long long>(missionId));
    }
}

}  // namespace

namespace game {

void watchOrders() {
    hooks::install("start command execute", ds2::at(kStartCommandExecute), reinterpret_cast<void*>(&executeDetour),
                   reinterpret_cast<void**>(&g_execute));
    hooks::install("start command commit", ds2::at(kStartCommit), reinterpret_cast<void*>(&commitDetour),
                   reinterpret_cast<void**>(&g_commit));
}

void blockOrders(bool block) {
    if (g_blocking.exchange(block) == block) return;
    logger::write("order_gate: accepting orders %s", block ? "sent to the host (guest)" : "allowed");
}

}  // namespace game
