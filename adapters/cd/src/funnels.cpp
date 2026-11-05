#include "funnels.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <cstring>
#include <mutex>

#include "build_gate.h"
#include "capture.h"
#include "cd_sites.h"
#include "crash_dump.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr size_t kOpcodes = 0x10000;
constexpr size_t kEventQueueLimit = 4096;  // a stalled main tick must not grow the queue without bound
constexpr size_t kInjectQueueLimit = 512;
constexpr size_t kPostQueueLimit = 16;
constexpr uint64_t kPostMaxWaitMs = 3000;  // a Req that waited longer (a menu, a load) names a world that may be gone
constexpr uint32_t kPostArgsSeen = 0x80000000u;
constexpr uint32_t kPostFlagsShift = 8;
constexpr uint32_t kPostChannelMask = 0xFF;
constexpr uint32_t kPostFlagsMask = 0xFFFF;
constexpr uint32_t kPostOk = 0;  // what ClientNet::PostPacket writes to its result when it queued the packet

// Both funnels are forwarded with eight integer arguments: the real ones (PostPacket has seven, OnRecvPacket five) plus
// caller stack slots that the callee ignores, so every argument reaches the original unchanged.
using Funnel = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// PostPacket(ClientNet*, int* result, session, packet, u16 size, channel, flags); OnRecvPacket(ClientNet*, NwSession*,
// unknown, packet, size) [N, STUDY.md 2.4]. The packet is the fourth argument of both and the size the fifth.
constexpr uint64_t kSizeMask = 0xFFFF;

Funnel g_postPacket = nullptr;
Funnel g_onRecvPacket = nullptr;
bool g_capture = false;

constexpr uint8_t kWatchHead = 1, kWatchFull = 2;
std::array<uint8_t, kOpcodes> g_watched{};  // 0, kWatchHead or kWatchFull; written before install only
std::array<std::atomic<uint32_t>, kOpcodes> g_counts[2];

std::atomic<uint64_t> g_packets[2], g_bytes[2], g_malformed, g_eventsDropped, g_faults, g_injected, g_injectDropped,
    g_injectFaults;
std::atomic<bool> g_injectionOn{true};
std::atomic<bool> g_postingOn{true};
std::atomic<uint64_t> g_posted, g_postDropped, g_postFaults;
std::array<std::atomic<uint32_t>, kOpcodes> g_postArgs{};  // per opcode: seen | flags << 8 | channel of its last real post
thread_local bool t_injecting = false;  // our own injected packet is passing the hook: not observed

std::mutex g_injectMutex;
std::vector<std::vector<uint8_t>> g_injectQueue;  // under g_injectMutex

struct QueuedPost {
    uint64_t queuedMs = 0;
    std::vector<uint8_t> packet;
};
std::mutex g_postMutex;
std::vector<QueuedPost> g_postQueue;  // under g_postMutex

// The held opcode (save_guard.cpp): while holding, the game's newest packet of it is kept here instead of sent.
std::atomic<uint16_t> g_holdOpcode{0};
std::atomic<bool> g_holding{false};
std::vector<uint8_t> g_held;  // under g_postMutex
std::atomic<uint64_t> g_heldCount{0};

std::mutex g_eventMutex;
std::vector<funnels::Event> g_events;  // under g_eventMutex

size_t index(packet::Direction direction) { return static_cast<size_t>(direction); }

void observe(packet::Direction direction, const uint8_t* bytes, uint32_t size, uint64_t session, uint64_t channel,
             uint64_t flags) {
    packet::Header header;
    if (!packet::parse(bytes, size, header)) {
        g_malformed.fetch_add(1, std::memory_order_relaxed);
        if (size < packet::kHeaderSize) return;
    }
    const size_t dir = index(direction);
    g_packets[dir].fetch_add(1, std::memory_order_relaxed);
    g_bytes[dir].fetch_add(size, std::memory_order_relaxed);
    g_counts[dir][header.opcode].fetch_add(1, std::memory_order_relaxed);
    if (g_capture) capture::record(direction, session, channel, flags, bytes, size);
    if (!g_watched[header.opcode]) return;
    funnels::Event event;
    event.direction = direction;
    event.header = header;
    const uint32_t payload = size - packet::kHeaderSize;
    std::memcpy(event.head.data(), bytes + packet::kHeaderSize, payload < funnels::kEventHeadBytes ? payload : funnels::kEventHeadBytes);
    if (g_watched[header.opcode] == kWatchFull) event.payload.assign(bytes + packet::kHeaderSize, bytes + size);
    std::lock_guard lock(g_eventMutex);
    if (g_events.size() >= kEventQueueLimit) {
        g_eventsDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_events.push_back(event);
}

// No C++ objects in the guarded functions, so __try can wrap them; a fault in our observer never reaches the game.
void observeGuarded(packet::Direction direction, uint64_t bytes, uint64_t size, uint64_t session, uint64_t channel,
                    uint64_t flags) {
    if (t_injecting) return;
    __try {
        observe(direction, reinterpret_cast<const uint8_t*>(bytes), static_cast<uint32_t>(size & kSizeMask), session,
                channel, flags);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "funnels: observer")) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
    }
}

void notePostArgs(uint64_t bytes, uint64_t channel, uint64_t flags) {
    uint16_t opcode = 0;
    __try {
        std::memcpy(&opcode, reinterpret_cast<const void*>(bytes), sizeof(opcode));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    g_postArgs[opcode].store(kPostArgsSeen | (static_cast<uint32_t>(flags & kPostFlagsMask) << kPostFlagsShift) |
                                 static_cast<uint32_t>(channel & kPostChannelMask),
                             std::memory_order_relaxed);
}

// One of our Reqs through the original PostPacket with the real call's ClientNet and session, and the channel and
// flags the game last used for that opcode (else the real call's). False on a fault.
bool postOneGuarded(uint64_t net, uint64_t session, const uint8_t* bytes, size_t size, uint64_t channel, uint64_t flags) {
    bool ok = true;
    __try {
        int result = 0;
        g_postPacket(net, reinterpret_cast<uint64_t>(&result), session, reinterpret_cast<uint64_t>(bytes), size, channel, flags, 0);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "funnels: posted packet")) {
        ok = false;
    }
    return ok;
}

void drainPosts(uint64_t net, uint64_t session, uint64_t channel, uint64_t flags) {
    if (!g_postingOn.load(std::memory_order_relaxed)) return;
    std::vector<QueuedPost> batch;
    {
        std::lock_guard lock(g_postMutex);
        if (g_postQueue.empty()) return;
        batch.swap(g_postQueue);
    }
    const uint64_t now = GetTickCount64();
    for (const QueuedPost& post : batch) {
        if (now - post.queuedMs > kPostMaxWaitMs) {
            g_postDropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        uint16_t opcode = 0;
        std::memcpy(&opcode, post.packet.data(), sizeof(opcode));
        const uint32_t seen = g_postArgs[opcode].load(std::memory_order_relaxed);
        const uint64_t useChannel = seen ? (seen & kPostChannelMask) : channel;
        const uint64_t useFlags = seen ? ((seen >> kPostFlagsShift) & kPostFlagsMask) : flags;
        if (!postOneGuarded(net, session, post.packet.data(), post.packet.size(), useChannel, useFlags)) {
            g_postFaults.fetch_add(1, std::memory_order_relaxed);
            g_postingOn.store(false, std::memory_order_relaxed);
            logger::write("funnels: a posted packet faulted, posting off for this session");
            return;
        }
        g_posted.fetch_add(1, std::memory_order_relaxed);
    }
}

// Keeps the game's packet of the held opcode while holding (the newest replaces an older one). True when kept.
bool holdPacket(uint64_t bytes, uint64_t size) {
    const uint16_t held = g_holdOpcode.load(std::memory_order_relaxed);
    const uint32_t length = static_cast<uint32_t>(size & kSizeMask);
    if (!held || !g_holding.load(std::memory_order_relaxed) || !bytes || length < packet::kHeaderSize) return false;
    const auto* data = reinterpret_cast<const uint8_t*>(bytes);
    uint16_t opcode = 0;
    std::memcpy(&opcode, data, sizeof(opcode));
    if (opcode != held) return false;
    std::lock_guard lock(g_postMutex);
    g_held.assign(data, data + length);
    g_heldCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

uint64_t __fastcall postPacketDetour(uint64_t net, uint64_t result, uint64_t session, uint64_t bytes, uint64_t size,
                                     uint64_t channel, uint64_t flags, uint64_t extra) {
    observeGuarded(packet::Direction::ClientToServer, bytes, size, session, channel, flags);
    notePostArgs(bytes, channel, flags);
    drainPosts(net, session, channel, flags);  // ours go in before this real one, on the game's own sending thread
    if (holdPacket(bytes, size)) {
        *reinterpret_cast<uint32_t*>(result) = kPostOk;
        return result;
    }
    return g_postPacket(net, result, session, bytes, size, channel, flags, extra);
}

// One injected packet through the real OnRecvPacket with the real call's first three arguments. False on a fault.
bool injectOneGuarded(uint64_t net, uint64_t session, uint64_t third, const uint8_t* bytes, size_t size) {
    t_injecting = true;
    bool ok = true;
    __try {
        g_onRecvPacket(net, session, third, reinterpret_cast<uint64_t>(bytes), size, 0, 0, 0);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "funnels: injected packet")) {
        ok = false;
    }
    t_injecting = false;
    return ok;
}

void drainInjections(uint64_t net, uint64_t session, uint64_t third) {
    if (t_injecting || !g_injectionOn.load(std::memory_order_relaxed)) return;
    std::vector<std::vector<uint8_t>> batch;
    {
        std::lock_guard lock(g_injectMutex);
        if (g_injectQueue.empty()) return;
        const size_t n = std::min(g_injectQueue.size(), funnels::kInjectPerCall);
        batch.assign(std::make_move_iterator(g_injectQueue.begin()), std::make_move_iterator(g_injectQueue.begin() + n));
        g_injectQueue.erase(g_injectQueue.begin(), g_injectQueue.begin() + n);
    }
    for (const std::vector<uint8_t>& packet : batch) {
        if (!injectOneGuarded(net, session, third, packet.data(), packet.size())) {
            g_injectFaults.fetch_add(1, std::memory_order_relaxed);
            g_injectionOn.store(false, std::memory_order_relaxed);
            logger::write("funnels: an injected packet faulted, injection off for this session");
            return;
        }
        g_injected.fetch_add(1, std::memory_order_relaxed);
    }
}

uint64_t __fastcall onRecvPacketDetour(uint64_t net, uint64_t session, uint64_t third, uint64_t bytes, uint64_t size,
                                       uint64_t a6, uint64_t a7, uint64_t a8) {
    drainInjections(net, session, third);
    observeGuarded(packet::Direction::ServerToClient, bytes, size, 0, 0, 0);
    return g_onRecvPacket(net, session, third, bytes, size, a6, a7, a8);
}

}  // namespace

namespace funnels {

void watch(uint16_t opcode, bool full) {
    g_watched[opcode] = std::max(g_watched[opcode], full ? kWatchFull : kWatchHead);
}

void inject(std::vector<uint8_t> packet) {
    if (!g_injectionOn.load(std::memory_order_relaxed)) {
        g_injectDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::lock_guard lock(g_injectMutex);
    if (g_injectQueue.size() >= kInjectQueueLimit) {
        g_injectDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_injectQueue.push_back(std::move(packet));
}

bool injectionOn() { return g_injectionOn.load(std::memory_order_relaxed); }

void post(std::vector<uint8_t> packet) {
    if (!g_postingOn.load(std::memory_order_relaxed) || packet.size() < packet::kHeaderSize) {
        g_postDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::lock_guard lock(g_postMutex);
    if (g_postQueue.size() >= kPostQueueLimit) {
        g_postDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_postQueue.push_back({GetTickCount64(), std::move(packet)});
}

bool postingOn() { return g_postingOn.load(std::memory_order_relaxed); }

void holdOpcode(uint16_t opcode) { g_holdOpcode.store(opcode, std::memory_order_relaxed); }

void setHolding(bool on) {
    if (on == g_holding.exchange(on, std::memory_order_relaxed) || on) return;
    std::vector<uint8_t> released;
    {
        std::lock_guard lock(g_postMutex);
        released.swap(g_held);
    }
    if (!released.empty()) post(std::move(released));  // the game's own packet, sent late
}

uint64_t heldCount() { return g_heldCount.load(std::memory_order_relaxed); }

bool install(bool capture) {
    if (!build_gate::setIntact(cd_sites::kFunnels)) {
        logger::write("funnels: not patched (site bytes differ)");
        return false;
    }
    g_capture = capture && capture::start();
    const bool post = hooks::install(cd_sites::kPostPacket.name, cd_sites::kPostPacket.address,
                                     reinterpret_cast<void*>(&postPacketDetour), reinterpret_cast<void**>(&g_postPacket));
    const bool recv = post && hooks::install(cd_sites::kOnRecvPacket.name, cd_sites::kOnRecvPacket.address,
                                             reinterpret_cast<void*>(&onRecvPacketDetour),
                                             reinterpret_cast<void**>(&g_onRecvPacket));
    if (post && !recv) hooks::remove(cd_sites::kPostPacket.address);
    if (recv) {
        logger::write("funnels: 2 patched (capture %s)", g_capture ? "on" : "off");
    } else {
        logger::write("funnels: not patched (hook install failed)");
    }
    return recv;
}

std::vector<Event> takeEvents() {
    std::vector<Event> out;
    std::lock_guard lock(g_eventMutex);
    out.swap(g_events);
    return out;
}

Totals totals() {
    Totals t;
    for (size_t dir = 0; dir < 2; ++dir) {
        t.packets[dir] = g_packets[dir].load(std::memory_order_relaxed);
        t.bytes[dir] = g_bytes[dir].load(std::memory_order_relaxed);
    }
    t.malformed = g_malformed.load(std::memory_order_relaxed);
    t.eventsDropped = g_eventsDropped.load(std::memory_order_relaxed);
    t.faults = g_faults.load(std::memory_order_relaxed);
    t.injected = g_injected.load(std::memory_order_relaxed);
    t.injectDropped = g_injectDropped.load(std::memory_order_relaxed);
    t.injectFaults = g_injectFaults.load(std::memory_order_relaxed);
    t.posted = g_posted.load(std::memory_order_relaxed);
    t.postDropped = g_postDropped.load(std::memory_order_relaxed);
    t.postFaults = g_postFaults.load(std::memory_order_relaxed);
    return t;
}

uint32_t opcodeCount(packet::Direction direction, uint16_t opcode) {
    return g_counts[index(direction)][opcode].load(std::memory_order_relaxed);
}

}  // namespace funnels
