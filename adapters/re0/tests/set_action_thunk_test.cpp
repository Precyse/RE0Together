// Checks the setAction thunk with fake classes whose slot takes 4, 2 and 1 stack arguments.
// Exit 0 when the handler and the original get the right arguments and the stack is balanced on every path.
#include <windows.h>

#include <cstdio>

#include "../src/set_action_thunk.h"

namespace {

constexpr size_t kFakeSlotCount = 70;
constexpr int32_t kArg[4] = {7, 8, 9, 10};

struct Fake {
    void** vtable;
};

int32_t g_originalArgs[4] = {};
int g_originalCalls = 0;
void* g_originalSelf = nullptr;
int32_t g_handlerArgs[4] = {};
int g_handlerCalls = 0;
void* g_handlerSelf = nullptr;
uintptr_t g_handlerOriginal = 0;
bool g_block = false;
size_t g_argc = 0;

void __fastcall original4(Fake* self, void*, int32_t a, int32_t b, int32_t c, int32_t d) {
    g_originalSelf = self;
    g_originalArgs[0] = a, g_originalArgs[1] = b, g_originalArgs[2] = c, g_originalArgs[3] = d;
    ++g_originalCalls;
}

void __fastcall original2(Fake* self, void*, int32_t a, int32_t b) {
    g_originalSelf = self;
    g_originalArgs[0] = a, g_originalArgs[1] = b;
    ++g_originalCalls;
}

void __fastcall original1(Fake* self, void*, int32_t a) {
    g_originalSelf = self;
    g_originalArgs[0] = a;
    ++g_originalCalls;
}

void __stdcall handler(void* enemy, uintptr_t original, const int32_t* args) {
    g_handlerSelf = enemy;
    g_handlerOriginal = original;
    for (size_t i = 0; i < g_argc; ++i) g_handlerArgs[i] = args[i];
    ++g_handlerCalls;
    if (g_block) return;
    auto* self = reinterpret_cast<void*>(enemy);
    if (g_argc == 4) game::callThiscall<void>(original, self, args[0], args[1], args[2], args[3]);
    else if (g_argc == 2) game::callThiscall<void>(original, self, args[0], args[1]);
    else game::callThiscall<void>(original, self, args[0]);
}

// Calls the slot with argc arguments and returns esp after minus esp before (0 when the thunk popped exactly them).
template <class Fn, class... Args>
__declspec(noinline) intptr_t callSlot(Fn slot, void* self, Args... args) {
    uintptr_t before = 0;
    uintptr_t after = 0;
    __asm mov before, esp
    slot(self, nullptr, args...);
    __asm mov after, esp
    return static_cast<intptr_t>(after - before);
}

int fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}

void reset() {
    g_originalCalls = g_handlerCalls = 0;
    for (int i = 0; i < 4; ++i) g_originalArgs[i] = g_handlerArgs[i] = 0;
}

int runCase(size_t argc, void* function) {
    void* table[kFakeSlotCount] = {};
    table[game::kEnemySetActionSlot] = function;
    Fake fake{table};
    auto* memory = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, set_action_thunk::kThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return fail("VirtualAlloc");
    const uintptr_t original =
        set_action_thunk::patchVtable(reinterpret_cast<uintptr_t>(table), memory, argc, handler);
    if (original != reinterpret_cast<uintptr_t>(function)) return fail("patchVtable did not return the original");
    g_argc = argc;
    reset();
    g_block = false;
    intptr_t unbalanced = 0;
    if (argc == 4) {
        unbalanced = callSlot(reinterpret_cast<void(__fastcall*)(void*, void*, int32_t, int32_t, int32_t, int32_t)>(table[game::kEnemySetActionSlot]), &fake, kArg[0], kArg[1], kArg[2], kArg[3]);
    } else if (argc == 2) {
        unbalanced = callSlot(reinterpret_cast<void(__fastcall*)(void*, void*, int32_t, int32_t)>(table[game::kEnemySetActionSlot]), &fake, kArg[0], kArg[1]);
    } else {
        unbalanced = callSlot(reinterpret_cast<void(__fastcall*)(void*, void*, int32_t)>(table[game::kEnemySetActionSlot]), &fake, kArg[0]);
    }
    if (unbalanced != 0) return fail("stack unbalanced (pass-through)");
    if (g_handlerCalls != 1 || g_originalCalls != 1) return fail("call counts");
    if (g_handlerSelf != &fake || g_originalSelf != &fake || g_handlerOriginal != original) return fail("self or original");
    for (size_t i = 0; i < argc; ++i) {
        if (g_handlerArgs[i] != kArg[i] || g_originalArgs[i] != kArg[i]) return fail("arguments");
    }
    g_block = true;
    if (argc == 4) {
        unbalanced = callSlot(reinterpret_cast<void(__fastcall*)(void*, void*, int32_t, int32_t, int32_t, int32_t)>(table[game::kEnemySetActionSlot]), &fake, kArg[0], kArg[1], kArg[2], kArg[3]);
    } else if (argc == 2) {
        unbalanced = callSlot(reinterpret_cast<void(__fastcall*)(void*, void*, int32_t, int32_t)>(table[game::kEnemySetActionSlot]), &fake, kArg[0], kArg[1]);
    } else {
        unbalanced = callSlot(reinterpret_cast<void(__fastcall*)(void*, void*, int32_t)>(table[game::kEnemySetActionSlot]), &fake, kArg[0]);
    }
    if (unbalanced != 0) return fail("stack unbalanced (blocked)");
    if (g_handlerCalls != 2 || g_originalCalls != 1) return fail("a blocked call ran the original");
    return 0;
}

}  // namespace

int main() {
    if (runCase(4, reinterpret_cast<void*>(original4)) != 0) return 1;
    if (runCase(2, reinterpret_cast<void*>(original2)) != 0) return 1;
    if (runCase(1, reinterpret_cast<void*>(original1)) != 0) return 1;
    std::printf("PASS\n");
    return 0;
}
