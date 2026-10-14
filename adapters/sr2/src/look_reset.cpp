#include "look_reset.h"

#include <cstring>
#include <vector>

#include "log.h"
#include "look_reset_rules.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

constexpr uintptr_t kHumanCharInstanceOffset = 0x13B0;  // pointer to the human's character instance
constexpr uintptr_t kCharInstancePoolIndexOffset = 0x10;
constexpr uintptr_t kEntrySlotOffset = 4;
constexpr uintptr_t kEntryTintAOffset = 8;
constexpr uintptr_t kEntryTintBOffset = 12;

bool g_trace = false;
const uintptr_t g_createCharInstance = sr2::kCharInstanceCreate;
const uintptr_t g_buildTintList = sr2::kTintListBuild;

// A1: `frame->args` are the create's arguments (render object, pool index, is local).
void __cdecl resetIndexLooks(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uint32_t poolIndex = args[1];
    for (const look_reset_rules::Range& range : look_reset_rules::rangesFor(poolIndex)) {
        memset(reinterpret_cast<void*>(range.address), 0, range.size);
    }
    if (g_trace) logger::write("trace: looks: cleared the tables of pool index %u", poolIndex);
}

TRACE_ENTRY_STUB(resetIndexLooksStub, resetIndexLooks, g_createCharInstance)

// T1: what the local human's tint list holds as a components message is built.
void __cdecl traceTintSend(uint32_t human) {
    const uint32_t poolIndex = readDword(readDword(human + kHumanCharInstanceOffset) + kCharInstancePoolIndexOffset);
    if (poolIndex >= look_reset_rules::kPoolIndices) return;
    const uint32_t count = readDword(look_reset_rules::kAppliedCounts + poolIndex * sizeof(uint32_t));
    const uintptr_t entries = look_reset_rules::kAppliedEntries + poolIndex * look_reset_rules::kTintRowBytes;
    std::vector<look_reset_rules::Tint> tints;
    for (uint32_t i = 0; i < count && i < look_reset_rules::kTintEntries; ++i) {
        const uintptr_t entry = entries + i * look_reset_rules::kTintEntryBytes;
        tints.push_back({readDword(entry + kEntrySlotOffset), readDword(entry + kEntryTintAOffset),
                         readDword(entry + kEntryTintBOffset)});
    }
    logger::write("trace: %s", look_reset_rules::describeSend(poolIndex, tints).c_str());
}

// Stands in for `call 0x7D84C0` (eax = the local human, ecx = where the length goes, the buffer on the stack, popped by the
// caller): the same call with the same arguments, then the trace; the result and the registers it gets are the same.
void __declspec(naked) traceTintSendStub() {
    __asm {
        push eax
        push ecx
        push dword ptr [esp + 12]
        call dword ptr [g_buildTintList]
        add esp, 4
        pop ecx
        pop edx
        push eax
        push ecx
        push edx
        call traceTintSend
        add esp, 4
        pop ecx
        pop eax
        ret
    }
}

}  // namespace

namespace look_reset {

void setTrace(bool trace) { g_trace = trace; }

uintptr_t resetIndexLooksThunk() { return reinterpret_cast<uintptr_t>(&resetIndexLooksStub); }

uintptr_t traceTintSendThunk() { return reinterpret_cast<uintptr_t>(&traceTintSendStub); }

}  // namespace look_reset
