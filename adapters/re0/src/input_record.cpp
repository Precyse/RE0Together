#include "input_record.h"

#include <windows.h>

#include <array>
#include <cstring>

#include "debug_stats.h"
#include "game.h"
#include "game_tick.h"
#include "input_redirect.h"
#include "log.h"
#include "pad_frame.h"

namespace {

using pad::PadFrame;

using QueryFunction = uint32_t(__fastcall*)(void* self, void* edx);
using StickFunction = void*(__fastcall*)(void* self, void* edx, void* out);
using ArgFunction = uint32_t(__fastcall*)(void* self, void* edx, uint32_t arg);

constexpr uint32_t kNeutralArg = 0;
constexpr size_t kStickScratchBytes = 64;  // the devices' out-struct size is only known to be >= 16
constexpr size_t kStickAlignment = 16;     // the joypad writes the stick vector with movaps

NetClient* g_net = nullptr;
std::array<uint32_t, pad::kVtableSlotCount> g_originals{};
std::array<bool, pad::kVtableSlotCount> g_faulted{};  // queries that raised once are skipped from then on
std::array<PadFrame, pad::kFramesPerPacket> g_history{};  // ring, indexed by frame % size
uint32_t g_frame = 0;

DWORD g_faultCode = 0;
uintptr_t g_faultAddress = 0;

int recordFault(EXCEPTION_POINTERS* info) {
    g_faultCode = info->ExceptionRecord->ExceptionCode;
    g_faultAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Isolated so SEH can guard one game call without C++ unwinding in the same frame.
bool callQuery(size_t slot, void* padObject, uint32_t& value, uint8_t* stickScratch) {
    const uintptr_t function = g_originals[slot];
    __try {
        if (slot == pad::kStickSlot) {
            reinterpret_cast<StickFunction>(function)(padObject, nullptr, stickScratch);
        } else if (slot == pad::kArgSlot) {
            value = reinterpret_cast<ArgFunction>(function)(padObject, nullptr, kNeutralArg);
        } else {
            value = reinterpret_cast<QueryFunction>(function)(padObject, nullptr);
        }
        return true;
    } __except (recordFault(GetExceptionInformation())) {
        return false;
    }
}

void evaluateQueries(void* padObject, PadFrame& out) {
    alignas(kStickAlignment) uint8_t stickScratch[kStickScratchBytes] = {};
    for (size_t slot = pad::kFirstQuerySlot; slot < pad::kFirstQuerySlot + pad::kQuerySlotCount; ++slot) {
        if (g_faulted[slot]) continue;
        if (!callQuery(slot, padObject, out.values[slot - pad::kFirstQuerySlot], stickScratch)) {
            g_faulted[slot] = true;
            debug_stats::setError("pad query slot %zu faulted", slot);
            logger::write("input_record: pad query slot %zu raised 0x%08lx at 0x%08x (pad 0x%08x, fn 0x%08x), skipping it",
                          slot, g_faultCode, static_cast<unsigned>(g_faultAddress),
                          static_cast<unsigned>(reinterpret_cast<uintptr_t>(padObject)), g_originals[slot]);
        }
    }
    std::memcpy(out.stick, stickScratch, sizeof(out.stick));
}

void send() {
    pad::PadPacket packet{};
    packet.count = g_frame < pad::kFramesPerPacket ? g_frame : static_cast<uint32_t>(pad::kFramesPerPacket);
    for (uint32_t i = 0; i < packet.count; ++i) {
        packet.frames[i] = g_history[(g_frame - packet.count + 1 + i) % g_history.size()];
    }
    const size_t size = sizeof(packet.count) + packet.count * sizeof(PadFrame);
    if (g_net->send(pad::kMsgPadFrame, false, proto::kSlotAll, {reinterpret_cast<const uint8_t*>(&packet), size})) {
        debug_stats::count(debug_stats::Counter::PadSent);
    }
}

void onTick() {
    void* padObject = input_redirect::realPad(0);
    if (!padObject) return;
    PadFrame& frame = g_history[(g_frame + 1) % g_history.size()];
    frame = PadFrame{};
    frame.frame = ++g_frame;
    if (game::isRealPad(padObject)) evaluateQueries(padObject, frame);  // blocked input records as nothing pressed
    const void* analog = input_redirect::realAnalog();
    if (analog) game::readMemory(reinterpret_cast<uintptr_t>(analog), frame.analog);
    send();
}

}  // namespace

namespace input_record {

bool captureOriginals() { return game::readMemory(game::kPadVtable, g_originals); }

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("input_record", onTick);
}

}  // namespace input_record
