#include "anim_stream_state.h"

#include <algorithm>

#include "anim_stream_sites.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

constexpr size_t kMaxPendingRead = 256;
constexpr size_t kMaxStreamEntriesRead = 4096;

}  // namespace

namespace anim_stream_state {

uint16_t readWord(uintptr_t address) {
    uint16_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

uint8_t slotOfMember(uintptr_t member) {
    return member ? readByte(member + sr2::kMemberSlotOffset) : anim_stream_rules::kNoSlot;
}

anim_trace_rules::NeedTableView needTableView() {
    return anim_trace_rules::needTableFromCode(readDword(anim_stream_sites::kNeedTableBaseImmediate),
                                               readDword(anim_stream_sites::kNeedTableEndImmediate));
}

std::vector<anim_trace_rules::NeedRow> needRows() {
    const anim_trace_rules::NeedTableView view = needTableView();
    std::vector<anim_trace_rules::NeedRow> rows;
    for (size_t i = 0; i < view.rows; ++i) {
        const uintptr_t row = view.base + i * sr2::kAnimNeedRowBytes;
        rows.push_back({readWord(row + sr2::kAnimNeedGroupOffset), readByte(row + sr2::kAnimNeedLocalOffset) != 0,
                        readByte(row + sr2::kAnimNeedRemoteOffset)});
    }
    return rows;
}

std::vector<anim_stream_rules::Pending> pendingRequests() {
    const uintptr_t list = readDword(sr2::kAnimPendingList);
    const size_t count = std::min<size_t>(readDword(sr2::kAnimPendingCount), kMaxPendingRead);
    std::vector<anim_stream_rules::Pending> pending;
    for (size_t i = 0; list != 0 && i < count; ++i) {
        const uintptr_t entry = list + i * sr2::kAnimPendingStride;
        pending.push_back({readWord(entry), readDword(entry + sr2::kAnimPendingIdOffset)});
    }
    return pending;
}

bool chunkLoaded(uint16_t group) {
    return group != sr2::kNoGroup && group < sr2::kChunkCount && readDword(sr2::kChunkLoadedTable + group * sizeof(uint32_t)) != 0;
}

bool streamNames(uint16_t group) {
    const uintptr_t state = readDword(sr2::kStreamState);
    if (state == 0) return false;
    const uint32_t kind = readDword(state + sr2::kStreamStateKindOffset);
    if (kind < sr2::kStreamStateKindFirst || kind > sr2::kStreamStateKindLast) return false;
    return readWord(readDword(state + sr2::kStreamStateObjectOffset) + sr2::kStreamObjectNamedGroupOffset) == group;
}

std::vector<anim_trace_rules::StreamEntry> streamEntries() {
    const uintptr_t entries = readDword(sr2::kStreamEntries);
    const size_t count = std::min<size_t>(readDword(sr2::kStreamEntryCount), kMaxStreamEntriesRead);
    std::vector<anim_trace_rules::StreamEntry> result;
    for (size_t i = 0; entries != 0 && i < count; ++i) {
        const uintptr_t entry = entries + i * sr2::kStreamEntryStride;
        const uint32_t state = readDword(entry + sr2::kStreamEntryStateOffset);
        if (!anim_trace_rules::entryInUse(state)) {
            result.push_back({state, 0, 0});
            continue;
        }
        const uintptr_t object = readDword(entry + sr2::kStreamEntryObjectOffset);
        result.push_back({state, readDword(object + sr2::kStreamObjectSlotOffset), readWord(object + sr2::kStreamObjectGroupOffset)});
    }
    return result;
}

uint8_t triggerLoadCategory() { return readByte(sr2::kTriggerPartnerLoadCategory); }

}  // namespace anim_stream_state
