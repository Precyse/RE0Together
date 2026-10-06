#include "ds2/queue_diag.h"

#include <atomic>
#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

// The engine's deferred message queue flush: (queues, list index, dispatch flag). Each list is {?, count at +8, array of
// message pointers at +0x10} at queues + index * 24; a flush runs Entity::Dispatch([message + 0x48], message) for each.
constexpr uintptr_t kQueueFlush = 0x14104e130;
constexpr uintptr_t kListSize = 24;
constexpr uintptr_t kListCount = 8;
constexpr uintptr_t kListMessages = 0x10;
constexpr uintptr_t kMessageEntity = 0x48;
constexpr int kMaxLogged = 20;
constexpr int kMaxScanned = 4096;

using FlushFn = void (*)(uintptr_t queues, uint8_t list, uint8_t dispatch);
FlushFn g_flush = nullptr;
std::atomic<uint64_t> g_flushes{0};
std::atomic<int> g_logged{0};

void flushDetour(uintptr_t queues, uint8_t list, uint8_t dispatch) {
    const uint64_t frame = g_flushes.fetch_add(1);
    const uintptr_t entry = queues + list * kListSize;
    int32_t count = 0;
    decima::safeRead(entry + kListCount, count);
    const uintptr_t messages = decima::readPointer(entry + kListMessages);
    for (int32_t i = 0; dispatch && messages && i < count && i < kMaxScanned && g_logged.load() < kMaxLogged; ++i) {
        const uintptr_t message = decima::readPointer(messages + i * sizeof(uintptr_t));
        uintptr_t entity = 1;
        if (!message || !decima::safeRead(message + kMessageEntity, entity) || entity) continue;
        g_logged.fetch_add(1);
        logger::write("queue_diag: flush %llu list %u entry %d of %d: message %p (vtable %p) has no entity; the remote body is %s",
                      static_cast<unsigned long long>(frame), list, i, count, reinterpret_cast<void*>(message),
                      reinterpret_cast<void*>(decima::readPointer(message) - ds2::at(ds2::kImageBase) + ds2::kImageBase),
                      remote_player::isParked() ? "parked" : remote_player::isLive() ? "live" : remote_player::entity() ? "spawning" : "absent");
    }
    g_flush(queues, list, dispatch);
}

}  // namespace

namespace queue_diag {

void installEarly() {
    hooks::install("message queue flush", ds2::at(kQueueFlush), reinterpret_cast<void*>(&flushDetour), reinterpret_cast<void**>(&g_flush));
}

}  // namespace queue_diag
