#include "pause_mask.h"

#include "memory.h"
#include "pause_mask_rules.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

const uintptr_t g_afterLeaveClear = sr2::kPauseLeaveResume;
const uintptr_t g_unpause = sr2::kUnpauseWorld;

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

// N4b: 1 when the leaver was the last one pausing, and the world is to be resumed.
uint32_t __cdecl clearLeaverBit(uintptr_t leaver) {
    uint8_t& flag = *reinterpret_cast<uint8_t*>(sr2::kPartnerPaused);
    const pause_mask_rules::Left left = pause_mask_rules::afterLeave(flag, slotOf(leaver));
    flag = left.mask;
    return left.resumes ? 1 : 0;
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

// Replaces the clear of the byte and the resume in the member left callback; ebx is the leaver.
void __declspec(naked) clearLeaverBitStub() {
    __asm {
        pushad
        push ebx
        call clearLeaverBit
        add esp, 4
        mov dword ptr [esp + 28], eax
        popad
        test eax, eax
        jz done
        call dword ptr [g_unpause]
    done:
        jmp dword ptr [g_afterLeaveClear]
    }
}

}  // namespace

namespace pause_mask {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::PauseSenderBit: return {reinterpret_cast<void*>(&setSenderBitStub), nullptr};
        case Thunk::PauseClearLeaver: return {reinterpret_cast<void*>(&clearLeaverBitStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace pause_mask
