#include "creation_queue.h"

#include "creation_queue_rules.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

uint8_t byteAt(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

}  // namespace

namespace creation_queue {

uint32_t queued(uintptr_t member) { return readDword(member + sr2::kMemberKnownCountOffset); }

bool enqueue(uintptr_t member, uintptr_t record) {
    const uint32_t count = queued(member);
    if (!creation_queue_rules::hasRoom(count)) return false;
    dwordField(member, sr2::kMemberKnownHandlesOffset + count * sizeof(uint32_t)) = readDword(record + sr2::kRecordHandleOffset);
    dwordField(member, sr2::kMemberKnownCountOffset) = count + 1;
    uint16_t mask = 0;
    readMemory(record + sr2::kRecordCreatedMaskOffset, &mask, sizeof(mask));
    mask = creation_queue_rules::maskAfterQueue(mask, byteAt(member + sr2::kMemberSlotOffset));
    *reinterpret_cast<uint16_t*>(record + sr2::kRecordCreatedMaskOffset) = mask;
    return true;
}

}  // namespace creation_queue
