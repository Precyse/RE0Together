#include "pause_mask.h"

#include "memory.h"
#include "pause_mask_rules.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

uint8_t byteAt(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

uint8_t slotOf(uintptr_t member) { return member ? byteAt(member + sr2::kMemberSlotOffset) : 0; }

// N4a: the handler 0x8CB110 stored the sender's flag (cl) in the byte; context is the second argument of the handler.
void __cdecl setSenderBit(uintptr_t context, uint32_t paused) {
    const uintptr_t sender = readDword(context + sr2::kMessageContextMemberOffset);
    uint8_t& flag = *reinterpret_cast<uint8_t*>(sr2::kPartnerPaused);
    flag = sender ? pause_mask_rules::afterMessage(flag, slotOf(sender), paused != 0) : static_cast<uint8_t>(paused != 0);
}

// Replaces `mov [0x252932F], cl` inside the handler, whose flags (from the compare before it) the next jump reads. The
// handler's arguments are payload and context; the call and pushfd put 8 bytes in front of them.
void __declspec(naked) setSenderBitStub() {
    __asm {
        pushfd
        pushad
        movzx eax, cl
        push eax
        push dword ptr [esp + 48]
        call setSenderBit
        add esp, 8
        popad
        popfd
        ret
    }
}

}  // namespace

namespace pause_mask {

bool afterLeave(uintptr_t leaver) {
    uint8_t& flag = *reinterpret_cast<uint8_t*>(sr2::kPartnerPaused);
    const pause_mask_rules::Left left = pause_mask_rules::afterLeave(flag, slotOf(leaver));
    flag = left.mask;
    return left.resumes;
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::PauseSenderBit: return {reinterpret_cast<void*>(&setSenderBitStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace pause_mask
