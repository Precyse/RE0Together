// The hook batch of the shared MinHook wrapper (hooks::beginBatch / remove / applyBatch) against small code stubs in the test's own memory:
// a hook taken back inside the batch stays off, also when the batch enable fails and the rest are enabled one by one.
#include <windows.h>

#include <cstdint>
#include <cstring>

#include "check.h"
#include "hooks.h"

namespace logger {
void write(const char*, ...) {}
}  // namespace logger

namespace debug_stats {
void setError(const char*, ...) {}
}  // namespace debug_stats

namespace {

constexpr size_t kStubSize = 16;
constexpr size_t kStubPageSize = 4096;
constexpr uint8_t kMovEaxImm32 = 0xB8;
constexpr uint8_t kRet = 0xC3;
constexpr uint8_t kInt3 = 0xCC;

// The values the stubs return, and the values their detours return in place of them.
constexpr int kKeptValue = 22;
constexpr int kKeptDetoured = 122;
constexpr int kTakenBackValue = 21;
constexpr int kTakenBackDetoured = 121;
constexpr int kBrokenValue = 7;
constexpr int kBrokenDetoured = 107;

int __cdecl keptDetour() { return kKeptDetoured; }
int __cdecl takenBackDetour() { return kTakenBackDetoured; }
int __cdecl brokenDetour() { return kBrokenDetoured; }

// `mov eax, value; ret` at `slot`, padded with int3.
uintptr_t writeStub(uint8_t* slot, int value) {
    memset(slot, kInt3, kStubSize);
    slot[0] = kMovEaxImm32;
    memcpy(slot + 1, &value, sizeof(value));
    slot[1 + sizeof(value)] = kRet;
    return reinterpret_cast<uintptr_t>(slot);
}

int run(uintptr_t stub) { return reinterpret_cast<int(__cdecl*)()>(stub)(); }

uint8_t* allocateCodePage() {
    return static_cast<uint8_t*>(VirtualAlloc(nullptr, kStubPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
}

void theTakenBackHookStaysOff() {
    uint8_t* page = allocateCodePage();
    const uintptr_t kept = writeStub(page, kKeptValue);
    const uintptr_t takenBack = writeStub(page + kStubSize, kTakenBackValue);
    void* original = nullptr;
    hooks::beginBatch();
    check(hooks::install("kept", kept, reinterpret_cast<void*>(&keptDetour), &original), "the kept hook installs in the batch");
    check(hooks::install("taken back", takenBack, reinterpret_cast<void*>(&takenBackDetour), &original), "the second hook installs in the batch");
    hooks::remove(takenBack);
    hooks::applyBatch();
    check(run(kept) == kKeptDetoured, "after the batch the kept hook is on");
    check(run(takenBack) == kTakenBackValue, "after the batch the hook taken back inside it is off");
}

void theTakenBackHookStaysOffWhenTheBatchEnableFails() {
    uint8_t* page = allocateCodePage();
    const uintptr_t kept = writeStub(page, kKeptValue);
    const uintptr_t takenBack = writeStub(page + kStubSize, kTakenBackValue);
    uint8_t* brokenPage = allocateCodePage();
    const uintptr_t broken = writeStub(brokenPage, kBrokenValue);
    void* original = nullptr;
    hooks::beginBatch();
    // The first hook MinHook enables is the first one created: the batch stops at the broken one and the rest go one by one.
    check(hooks::install("broken", broken, reinterpret_cast<void*>(&brokenDetour), &original), "the hook that will fail installs in the batch");
    check(hooks::install("kept", kept, reinterpret_cast<void*>(&keptDetour), &original), "the kept hook installs in the batch");
    check(hooks::install("taken back", takenBack, reinterpret_cast<void*>(&takenBackDetour), &original), "the taken back hook installs in the batch");
    hooks::remove(takenBack);
    VirtualFree(brokenPage, 0, MEM_RELEASE);
    hooks::applyBatch();
    check(run(kept) == kKeptDetoured, "a failed batch enable still turns the kept hook on one by one");
    check(run(takenBack) == kTakenBackValue, "a failed batch enable does not turn the taken back hook on again");
}

}  // namespace

int main() {
    theTakenBackHookStaysOff();
    theTakenBackHookStaysOffWhenTheBatchEnableFails();
    return g_failures == 0 ? 0 : 1;
}
