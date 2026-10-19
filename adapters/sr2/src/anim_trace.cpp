#include "anim_trace.h"

#include <cstdio>
#include <iterator>
#include <mutex>

#include "anim_trace_rules.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using anim_trace_rules::NeedRow;

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;
constexpr size_t kSlotArg = 0;
constexpr size_t kEntryArg = 0;
constexpr uintptr_t kSyncGroupOffset = 1;  // word: the anim group, after the sub id
constexpr uint8_t kNoSlot = 0xFF;
constexpr size_t kAnimSlots = std::size(sr2::kAnimSlotPools);

void* g_syncOriginal = nullptr;
void* g_unloadOriginal = nullptr;
void* g_deltaOriginal = nullptr;
std::mutex g_mutex;
rate_limit::OncePerKey g_seen;  // the (entry, data) pairs already logged

uint16_t readWord(uintptr_t address) {
    uint16_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// The session slot of the member a message came from (the context's [+4]), kNoSlot for none.
uint8_t senderSlot(uintptr_t context) {
    const uintptr_t member = readDword(context + sr2::kMessageContextMemberOffset);
    return member ? readByte(member + sr2::kMemberSlotOffset) : kNoSlot;
}

std::string needTable() {
    NeedRow rows[sr2::kAnimNeedRows];
    for (size_t i = 0; i < sr2::kAnimNeedRows; ++i) {
        const uintptr_t row = sr2::kAnimNeedTable + i * sr2::kAnimNeedRowBytes;
        rows[i] = {readWord(row + sr2::kAnimNeedGroupOffset), readByte(row + sr2::kAnimNeedLocalOffset) != 0,
                   readByte(row + sr2::kAnimNeedRemoteOffset) != 0};
    }
    return anim_trace_rules::needTableText(rows, sr2::kAnimNeedRows);
}

// AN1: the game's handler runs, then the line names the message and the need table it left.
void __cdecl traceStreamingSync(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    const uintptr_t context = frame->args[kContextArg];
    const unsigned sub = readByte(payload);
    const unsigned group = readWord(payload + kSyncGroupOffset);
    const unsigned slot = senderSlot(context);
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_syncOriginal), frame->regs, {static_cast<uint32_t>(payload), static_cast<uint32_t>(context)});
    logger::write("trace: anim sets: streaming_sync sub %u group 0x%04X from slot %u, need table %s", sub, group, slot, needTable().c_str());
}

// AN2: a chunk anim slot is released (thiscall: ecx = the group, the slot on the stack).
void __cdecl traceAnimUnload(GuestFrame* frame) {
    const uint32_t slot = frame->args[kSlotArg];
    const uint32_t used = slot < kAnimSlots ? readDword(sr2::kAnimSlotPools[slot] + sr2::kPoolUsedOffset) : 0;
    logger::write("trace: anim sets: unload group 0x%04X slot %u, slot used 0x%X", frame->regs.ecx & 0xFFFF, slot, used);
}

// AN3: the root motion decode is about to read an anim whose header offset cannot be right; once per (entry, data).
void __cdecl traceAnimDelta(GuestFrame* frame) {
    const uintptr_t entry = frame->args[kEntryArg];
    const uint32_t data = readDword(entry + sr2::kAnimEntryDataOffset);
    if (data == 0) return;
    const uint32_t size = readDword(entry + sr2::kAnimEntrySizeOffset);
    const uint32_t headerOffset = readDword(data + sr2::kAnimHeaderOffsetOffset);
    if (!anim_trace_rules::undecodable(data, readByte(data + sr2::kAnimHeaderFlagsOffset), headerOffset, size)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_seen.firstTime((static_cast<uint64_t>(entry) << 32) | data)) return;
    anim_trace_rules::Slot slots[kAnimSlots];
    for (size_t i = 0; i < kAnimSlots; ++i) {
        slots[i] = {readDword(sr2::kAnimSlotPools[i] + sr2::kPoolBaseOffset), readDword(sr2::kAnimSlotPools[i] + sr2::kPoolSizeOffset)};
    }
    const int slot = anim_trace_rules::slotOf(data, slots, kAnimSlots);
    char where[24] = "outside";
    if (slot >= 0) snprintf(where, sizeof(where), "in chunk_anim%d", slot);
    logger::write("trace: anim: entry 0x%X hash 0x%08X data 0x%08X header offset 0x%X size 0x%X %s", static_cast<unsigned>(entry),
                  readDword(entry + sr2::kAnimEntryHashOffset), data, headerOffset, size, where);
}

PARTNER_WRAP_STUB(streamingSyncStub, traceStreamingSync, 0)
TRACE_ENTRY_STUB(animUnloadStub, traceAnimUnload, g_unloadOriginal)
TRACE_ENTRY_STUB(animDeltaStub, traceAnimDelta, g_deltaOriginal)

}  // namespace

namespace anim_trace {

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::TraceStreamingSync: return {reinterpret_cast<void*>(&streamingSyncStub), &g_syncOriginal};
        case partner_sites::Thunk::TraceAnimUnload: return {reinterpret_cast<void*>(&animUnloadStub), &g_unloadOriginal};
        case partner_sites::Thunk::TraceAnimDelta: return {reinterpret_cast<void*>(&animDeltaStub), &g_deltaOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace anim_trace
