#include "creation_queue.h"

#include <mutex>

#include "creation_queue_rules.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

std::mutex g_overflowLock;
creation_queue_rules::Overflow g_overflow;
bool g_trace = false;

void* g_drainOriginal = nullptr;
const uintptr_t g_resumeCreate = sr2::kQueueAppendResumeCreate;
const uintptr_t g_resumeBatch = sr2::kQueueAppendResumeBatch;

// After the game's drain emptied the queue: the oldest handles that did not fit go in, to be sent on the next tick.
void refill(uintptr_t member) {
    uint32_t handles[creation_queue_rules::kCapacity];
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g_overflowLock);
        if (!g_overflow.pending(member)) return;
        count = g_overflow.take(member, handles, creation_queue_rules::kCapacity);
    }
    for (size_t i = 0; i < count; ++i) dwordField(member, sr2::kMemberKnownHandlesOffset + i * sizeof(uint32_t)) = handles[i];
    dwordField(member, sr2::kMemberKnownCountOffset) = static_cast<uint32_t>(count);
}

// W8a (0x805000, eax = member, edi = handle) and W8b (0x8051F0, edx = member, ebx = handle) replace the unchecked append.
void __cdecl appendHandle(uintptr_t member, uint32_t handle) { creation_queue::append(member, handle); }

// Replaces the append and the load of the member's slot into cl, which the code after it uses.
void __declspec(naked) appendCreateStub() {
    __asm {
        pushad
        push edi
        push eax
        call appendHandle
        add esp, 8
        popad
        mov cl, byte ptr [eax + 0x1b0]
        jmp dword ptr [g_resumeCreate]
    }
}

// Replaces the append in the join batch; ebx is left as the original left it, the handle of the record.
void __declspec(naked) appendBatchStub() {
    __asm {
        mov ebx, dword ptr [eax + 4]
        pushad
        push ebx
        push edx
        call appendHandle
        add esp, 8
        popad
        jmp dword ptr [g_resumeBatch]
    }
}

// W8c (0x804940, eax = member, edx = handle) is the whole append function.
void __declspec(naked) appendFunctionStub() {
    __asm {
        push eax
        push edx
        push edx
        push eax
        call appendHandle
        add esp, 8
        pop edx
        pop eax
        ret
    }
}

// W8d (0x807840, ecx = member): the game's drain, then the overflow moves in.
void __cdecl drainThenRefill(GuestFrame* frame) {
    const uintptr_t member = frame->regs.ecx;
    guest_call::call(reinterpret_cast<uintptr_t>(g_drainOriginal), frame->regs);
    refill(member);
}

PARTNER_WRAP_STUB(drainStub, drainThenRefill, 0)

}  // namespace

namespace creation_queue {

uint32_t queued(uintptr_t member) { return readDword(member + sr2::kMemberKnownCountOffset); }

void setTrace(bool trace) { g_trace = trace; }

void append(uintptr_t member, uint32_t handle) {
    const uint32_t count = queued(member);
    if (creation_queue_rules::hasRoom(count)) {
        dwordField(member, sr2::kMemberKnownHandlesOffset + count * sizeof(uint32_t)) = handle;
        dwordField(member, sr2::kMemberKnownCountOffset) = count + 1;
        return;
    }
    std::lock_guard<std::mutex> lock(g_overflowLock);
    g_overflow.add(member, handle);
    if (g_trace) {
        logger::writeUnlessRepeated("trace: creation queue of slot %u is full, %zu handles wait", readByte(member + sr2::kMemberSlotOffset),
                                    g_overflow.pending(member));
    }
}

bool enqueue(uintptr_t member, uintptr_t record) {
    const bool fits = creation_queue_rules::hasRoom(queued(member));
    append(member, readDword(record + sr2::kRecordHandleOffset));
    uint16_t mask = 0;
    readMemory(record + sr2::kRecordCreatedMaskOffset, &mask, sizeof(mask));
    mask = creation_queue_rules::maskAfterQueue(mask, readByte(member + sr2::kMemberSlotOffset));
    *reinterpret_cast<uint16_t*>(record + sr2::kRecordCreatedMaskOffset) = mask;
    return fits;
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::QueueAppendCreate: return {reinterpret_cast<void*>(&appendCreateStub), nullptr};
        case Thunk::QueueAppendBatch: return {reinterpret_cast<void*>(&appendBatchStub), nullptr};
        case Thunk::QueueAppendFunction: return {reinterpret_cast<void*>(&appendFunctionStub), nullptr};

        case Thunk::QueueDrain: return {reinterpret_cast<void*>(&drainStub), &g_drainOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace creation_queue
