#include "net_pad.h"

#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

#include "debug_stats.h"
#include "game.h"
#include "log.h"
#include "pad_frame.h"

namespace {

using pad::PadFrame;

constexpr size_t kTargetBuffered = 3;
constexpr size_t kMaxBehind = 10;
constexpr size_t kMaxBuffered = 64;
constexpr int kNoPeer = -1;

std::mutex g_mutex;
std::map<uint32_t, PadFrame> g_buffer;  // guarded by g_mutex
uint32_t g_lastConsumed = 0;            // guarded by g_mutex
bool g_hasConsumed = false;             // guarded by g_mutex
bool g_started = false;                 // guarded by g_mutex
bool g_resetCurrent = false;            // guarded by g_mutex
std::atomic<int> g_peerSlot{kNoPeer};

PadFrame g_current{};  // game thread only; all zeros is the neutral pad
game::PadBytes g_object{};
std::array<uint32_t, pad::kVtableSlotCount> g_vtable{};
bool g_objectReady = false;

template <size_t Slot>
uint32_t __fastcall queryThunk(void*, void*) {
    return g_current.values[Slot - pad::kFirstQuerySlot];
}

void* __fastcall stickThunk(void*, void*, void* out) {
    std::memcpy(out, g_current.stick, sizeof(g_current.stick));
    return out;
}

uint32_t __fastcall argThunk(void*, void*, uint32_t) { return g_current.values[pad::kArgSlot - pad::kFirstQuerySlot]; }

template <size_t Slot>
uint32_t thunkFor() {
    if constexpr (Slot == pad::kStickSlot) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&stickThunk));
    else if constexpr (Slot == pad::kArgSlot) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&argThunk));
    else return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&queryThunk<Slot>));
}

template <size_t... Offsets>
void installThunks(std::index_sequence<Offsets...>) {
    ((g_vtable[pad::kFirstQuerySlot + Offsets] = thunkFor<pad::kFirstQuerySlot + Offsets>()), ...);
}

bool createObject(void* realPad) {
    std::array<uint32_t, pad::kVtableSlotCount> original{};
    if (!game::readMemory(reinterpret_cast<uintptr_t>(realPad), g_object) ||
        !game::readMemory(game::kPadVtable, original)) {
        return false;
    }
    g_vtable = original;
    installThunks(std::make_index_sequence<pad::kQuerySlotCount>{});
    const uint32_t vtableAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_vtable.data()));
    std::memcpy(g_object.data(), &vtableAddress, sizeof(vtableAddress));
    logger::write("net_pad: object at 0x%x", static_cast<unsigned>(reinterpret_cast<uintptr_t>(g_object.data())));
    return true;
}

void clearBufferLocked() {
    g_buffer.clear();
    g_hasConsumed = false;
    g_started = false;
    g_resetCurrent = true;
}

}  // namespace

namespace net_pad {

void onSession(const SessionSnapshot& session) {
    int slot = kNoPeer;
    if (session.linked) {
        for (const PeerInfo& peer : session.peers) {
            if (peer.slot != session.localSlot) {
                slot = peer.slot;
                break;
            }
        }
    }
    if (g_peerSlot.exchange(slot) == slot) return;
    std::lock_guard lock(g_mutex);
    clearBufferLocked();
    logger::write("net_pad: driving peer slot=%d", slot);
}

void onPacket(const GameFrame& frame) {
    if (frame.slot != g_peerSlot.load() || frame.payload.size() < sizeof(uint32_t)) return;
    pad::PadPacket packet{};
    if (frame.payload.size() > sizeof(packet)) return;
    std::memcpy(&packet, frame.payload.data(), frame.payload.size());
    if (packet.count > pad::kFramesPerPacket ||
        frame.payload.size() != sizeof(packet.count) + packet.count * sizeof(PadFrame)) {
        return;
    }
    debug_stats::count(debug_stats::Counter::PadReceived);
    std::lock_guard lock(g_mutex);
    for (uint32_t i = 0; i < packet.count; ++i) {
        const PadFrame& received = packet.frames[i];
        if (g_hasConsumed && received.frame <= g_lastConsumed) continue;
        g_buffer.emplace(received.frame, received);
    }
    while (g_buffer.size() > kMaxBuffered) g_buffer.erase(g_buffer.begin());
}

bool active() { return g_peerSlot.load() != kNoPeer; }

int peerSlot() { return g_peerSlot.load(); }

void advance() {
    std::lock_guard lock(g_mutex);
    if (g_resetCurrent) {
        g_current = PadFrame{};
        g_resetCurrent = false;
    }
    if (!g_started && g_buffer.size() >= kTargetBuffered) g_started = true;
    if (!g_started) return;
    if (g_buffer.empty()) {
        g_started = false;  // underrun: the last frame repeats while the buffer refills
        debug_stats::count(debug_stats::Counter::PadUnderruns);
        return;
    }
    if (g_buffer.size() > kMaxBehind) {
        for (size_t excess = g_buffer.size() - kTargetBuffered; excess > 0; --excess) {
            g_buffer.erase(g_buffer.begin());
            debug_stats::count(debug_stats::Counter::PadSkips);
        }
    }
    const auto oldest = g_buffer.begin();
    g_current = oldest->second;
    g_lastConsumed = oldest->first;
    g_hasConsumed = true;
    g_buffer.erase(oldest);
    debug_stats::set(debug_stats::Gauge::PadDepth, static_cast<int>(g_buffer.size()));
}

void* object(void* realPad) {
    if (!g_objectReady) g_objectReady = createObject(realPad);
    return g_objectReady ? g_object.data() : realPad;
}

void* analog() { return g_current.analog; }

}  // namespace net_pad
