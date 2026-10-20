#include "anim_trace.h"

#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "activity_state.h"
#include "anim_stream_rules.h"
#include "anim_stream_state.h"
#include "anim_trace_rules.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;
constexpr size_t kSlotArg = 0;
constexpr size_t kEntryArg = 0;
constexpr size_t kLoadGroupArg = 1;
constexpr size_t kLoadSlotArg = 3;
constexpr size_t kAnimSlots = std::size(sr2::kAnimSlotPools);

void* g_syncOriginal = nullptr;
void* g_unloadOriginal = nullptr;
void* g_deltaOriginal = nullptr;
void* g_loadOriginal = nullptr;
std::mutex g_mutex;
rate_limit::OncePerKey g_seen;  // the (entry, data) pairs already logged

// How much of chunk anim slot `slot`'s pool is used, 0 for a slot the game does not have.
uint32_t slotUsed(uint32_t slot) { return slot < kAnimSlots ? readDword(sr2::kAnimSlotPools[slot] + sr2::kPoolUsedOffset) : 0; }

std::string needTable() {
    const std::vector<anim_trace_rules::NeedRow> rows = anim_stream_state::needRows();
    return anim_trace_rules::needTableText(rows.data(), rows.size());
}

// ", count C, answer loads Y" of an answer: whether the game's 0xA83340 will load the set, read before the handler removes the pending entry.
std::string answerText(uintptr_t payload, uint16_t group, uint32_t id) {
    const bool loads = anim_stream_rules::answerWouldLoad(anim_stream_state::chunkLoaded(group), anim_stream_state::streamNames(group),
                                                         anim_stream_rules::pendingFor(anim_stream_state::pendingRequests(), group), id);
    char text[48];
    std::snprintf(text, sizeof(text), ", count %u, answer loads %c", readDword(payload + sr2::kStreamingCountOffset), loads ? 'Y' : 'N');
    return text;
}

// AN1: the message and what the game's handler will do with it are read, the handler runs, then the line names the need table it left.
void __cdecl traceStreamingSync(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    const uintptr_t context = frame->args[kContextArg];
    const uint8_t sub = readByte(payload);
    const uint16_t group = readWord(payload + sr2::kStreamingGroupOffset);
    const unsigned from = anim_stream_state::slotOfMember(activity_state::senderOf(context));
    std::string detail;
    std::string answer;
    if (sub <= sr2::kStreamingSubAnswer) {
        const uint32_t id = readDword(payload + sr2::kStreamingIdOffset);
        char slotText[24];
        std::snprintf(slotText, sizeof(slotText), " slot %u", readDword(payload + sr2::kStreamingSlotOffset));
        detail = anim_trace_rules::syncIdText(sub, id) + slotText;
        if (sub == sr2::kStreamingSubAnswer) answer = answerText(payload, group, id);
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_syncOriginal), frame->regs, {static_cast<uint32_t>(payload), static_cast<uint32_t>(context)});
    logger::write("trace: anim sets: streaming_sync sub %u group 0x%04X%s from slot %u%s, need table %s", sub, group, detail.c_str(), from,
                  answer.c_str(), needTable().c_str());
}

// AN2: a chunk anim slot is released (thiscall: ecx = the group, the slot on the stack). The other sets in the slot are listed: a slot holds
// one set, so any is a bystander or a duplicate load.
void __cdecl traceAnimUnload(GuestFrame* frame) {
    const uint32_t slot = frame->args[kSlotArg];
    const uint16_t group = frame->regs.ecx & 0xFFFF;
    std::vector<uint16_t> others;
    if (slot < kAnimSlots) others = anim_trace_rules::otherSetsInSlot(anim_stream_state::streamEntries(), anim_stream_state::slotDescriptor(slot), group);
    logger::write("trace: anim sets: unload group 0x%04X slot %u, slot used 0x%X, other sets in the slot %s", group, slot, slotUsed(slot),
                  anim_trace_rules::groupsText(others).c_str());
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

// AN4: an anim set is loaded into a chunk anim slot (cdecl(out, group, name, slot, list, count)). The caller says whose set it is.
void __cdecl traceAnimLoad(GuestFrame* frame) {
    const uint16_t group = frame->args[kLoadGroupArg] & 0xFFFF;
    const uint32_t slot = frame->args[kLoadSlotArg];
    char source[16];
    std::snprintf(source, sizeof(source), "0x%08X", frame->returnAddress);
    const char* from = frame->returnAddress == sr2::kAnimLoadFromAnswer ? "an answer" : frame->returnAddress == sr2::kAnimLoadFromRequest ? "our request" : source;
    logger::write("trace: anim sets: load group 0x%04X slot %u from %s, slot used 0x%X", group, slot, from, slotUsed(slot));
}

PARTNER_WRAP_STUB(streamingSyncStub, traceStreamingSync, 0)
TRACE_ENTRY_STUB(animUnloadStub, traceAnimUnload, g_unloadOriginal)
TRACE_ENTRY_STUB(animDeltaStub, traceAnimDelta, g_deltaOriginal)
TRACE_ENTRY_STUB(animLoadStub, traceAnimLoad, g_loadOriginal)

}  // namespace

namespace anim_trace {

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::TraceStreamingSync: return {reinterpret_cast<void*>(&streamingSyncStub), &g_syncOriginal};
        case partner_sites::Thunk::TraceAnimUnload: return {reinterpret_cast<void*>(&animUnloadStub), &g_unloadOriginal};
        case partner_sites::Thunk::TraceAnimDelta: return {reinterpret_cast<void*>(&animDeltaStub), &g_deltaOriginal};
        case partner_sites::Thunk::TraceAnimLoad: return {reinterpret_cast<void*>(&animLoadStub), &g_loadOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace anim_trace
