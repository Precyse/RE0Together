#include "ambient_guard.h"

#include <windows.h>

#include <mutex>

#include "ambient_guard_rules.h"
#include "log.h"
#include "sr2_addresses.h"

namespace {

std::mutex g_mutex;
ambient_guard_rules::RateLimit g_rateLimit;
bool g_trace = false;
const uintptr_t g_useTemplate = sr2::kAmbientTemplateUse;
const uintptr_t g_nextElement = sr2::kAmbientNextElement;

// A skipped element: `element` is the element pointer (0 when the element itself is null), `index` its place in the group.
void __cdecl noteSkippedElement(uintptr_t element, uint32_t index) {
    if (!g_trace) return;
    uint32_t skipped = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_rateLimit.allow(GetTickCount64(), skipped)) return;
    }
    logger::write("trace: ambient spawner skipped a %s at index %u (%u since the last line)",
                  element ? "null template" : "null element", index, skipped);
}

// Replaces `mov esi, [ebp]; mov ecx, [esi+0x34]` of the spawner's loop: the same loads, and an element or template that is
// null goes to the next element instead. ebx, edi and ebp are kept; esi and ecx are loaded as the original does.
void __declspec(naked) skipNullElementStub() {
    __asm {
        mov esi, dword ptr [ebp]
        test esi, esi
        jz skip
        mov ecx, dword ptr [esi + 0x34]
        test ecx, ecx
        jz skip
        jmp dword ptr [g_useTemplate]
    skip:
        pushad
        pushfd
        push edi
        push esi
        call noteSkippedElement
        add esp, 8
        popfd
        popad
        jmp dword ptr [g_nextElement]
    }
}

}  // namespace

namespace ambient_guard {

void setTrace(bool trace) { g_trace = trace; }

uintptr_t skipNullElementThunk() { return reinterpret_cast<uintptr_t>(&skipNullElementStub); }

}  // namespace ambient_guard
