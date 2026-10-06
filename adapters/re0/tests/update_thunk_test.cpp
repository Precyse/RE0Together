// Checks the enemy update thunk against a fake object whose vtable slot 41 is a plain thiscall function.
// Exit 0 when the handler and the original receive the right arguments and the stack is balanced on every path.
#include <windows.h>

#include <cstdio>

#include "../src/update_thunk.h"

namespace {

constexpr size_t kFakeSlotCount = 50;
constexpr int kPassCount = 3;

struct Fake {
    void** vtable;
};

void* g_originalSelf = nullptr;
int g_originalCalls = 0;
void* g_handlerSelf = nullptr;
uintptr_t g_handlerOriginal = 0;
int g_handlerCalls = 0;
bool g_skip = false;

void __fastcall fakeUpdate(Fake* self, void*) {
    g_originalSelf = self;
    ++g_originalCalls;
}

void __stdcall handler(void* enemy, uintptr_t original) {
    g_handlerSelf = enemy;
    g_handlerOriginal = original;
    ++g_handlerCalls;
    if (!g_skip) update_thunk::callOriginal(original, enemy);
}

using UpdateFn = void(__fastcall*)(void* self, void* edx);

// Returns esp after the call minus esp before it: 0 when the thunk pops nothing.
__declspec(noinline) intptr_t callSlot(UpdateFn slot, void* self) {
    uintptr_t before = 0;
    uintptr_t after = 0;
    __asm mov before, esp
    slot(self, nullptr);
    __asm mov after, esp
    return static_cast<intptr_t>(after - before);
}

int fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}

}  // namespace

int main() {
    void* table[kFakeSlotCount] = {};
    table[game::kEnemyUpdateSlot] = reinterpret_cast<void*>(fakeUpdate);
    Fake fake{table};
    auto* memory = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, update_thunk::kThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return fail("VirtualAlloc");

    const uintptr_t original = update_thunk::patchVtable(reinterpret_cast<uintptr_t>(table), memory, handler);
    if (original != reinterpret_cast<uintptr_t>(fakeUpdate)) return fail("patchVtable did not return the original");
    if (table[game::kEnemyUpdateSlot] != memory) return fail("slot not patched");

    auto* thunked = reinterpret_cast<UpdateFn>(table[game::kEnemyUpdateSlot]);
    for (int pass = 1; pass <= kPassCount; ++pass) {
        if (callSlot(thunked, &fake) != 0) return fail("stack unbalanced (pass-through)");
        if (g_handlerCalls != pass || g_originalCalls != pass) return fail("call counts");
        if (g_handlerSelf != &fake || g_originalSelf != &fake || g_handlerOriginal != original) {
            return fail("arguments");
        }
    }

    g_skip = true;
    if (callSlot(thunked, &fake) != 0) return fail("stack unbalanced (skipped)");
    if (g_handlerCalls != kPassCount + 1 || g_originalCalls != kPassCount) return fail("skip ran the original");

    std::printf("PASS\n");
    return 0;
}
