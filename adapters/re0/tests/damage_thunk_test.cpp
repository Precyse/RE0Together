// Checks the enemy damage thunk against a fake object whose vtable slot 35 is a thiscall function with ret 0xC.
// Exit 0 when the handler and the original receive the right arguments and the stack is balanced on every path.
#include <windows.h>

#include <cstdio>

#include "../src/damage_thunk.h"

namespace {

constexpr size_t kFakeSlotCount = 40;
constexpr int kPassCount = 3;

struct Fake {
    void** vtable;
};

struct Recorded {
    void* enemy = nullptr;
    void* attacker = nullptr;
    game::HitPoint* point = nullptr;
    game::HitInfo* info = nullptr;
    int calls = 0;
};

Recorded g_original;
Recorded g_handler;
uintptr_t g_handlerOriginal = 0;
bool g_suppress = false;

void __fastcall fakeDamage(Fake* self, void*, void* attacker, game::HitPoint* point, game::HitInfo* info) {
    g_original = {self, attacker, point, info, g_original.calls + 1};
}

void __stdcall handler(void* enemy, void* attacker, game::HitPoint* point, game::HitInfo* info, uintptr_t original) {
    g_handler = {enemy, attacker, point, info, g_handler.calls + 1};
    g_handlerOriginal = original;
    if (!g_suppress) damage_thunk::callOriginal(original, enemy, attacker, point, info);
}

using DamageFn = void(__fastcall*)(void* self, void* edx, void* attacker, game::HitPoint* point, game::HitInfo* info);

// Returns esp after the call minus esp before it: 0 when the thunk pops exactly its 12 argument bytes.
__declspec(noinline) intptr_t callSlot(DamageFn slot, void* self, void* attacker, game::HitPoint* point,
                                       game::HitInfo* info) {
    uintptr_t before = 0;
    uintptr_t after = 0;
    __asm mov before, esp
    slot(self, nullptr, attacker, point, info);
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
    table[game::kEnemyDamageSlot] = reinterpret_cast<void*>(fakeDamage);
    Fake fake{table};
    auto* memory = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, damage_thunk::kThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return fail("VirtualAlloc");

    const uintptr_t original = damage_thunk::patchVtable(reinterpret_cast<uintptr_t>(table), memory, handler);
    if (original != reinterpret_cast<uintptr_t>(fakeDamage)) return fail("patchVtable did not return the original");
    if (table[game::kEnemyDamageSlot] != memory) return fail("slot not patched");

    game::HitInfo info{1, 2, 3, 4, &fake, 1, {}};
    game::HitPoint point{1.0f, 2.0f, 3.0f};
    int attackerTarget = 0;
    auto* thunked = reinterpret_cast<DamageFn>(table[game::kEnemyDamageSlot]);

    for (int pass = 1; pass <= kPassCount; ++pass) {
        if (callSlot(thunked, &fake, &attackerTarget, &point, &info) != 0) return fail("stack unbalanced (pass-through)");
        if (g_handler.calls != pass || g_original.calls != pass) return fail("call counts");
        if (g_handler.enemy != &fake || g_handler.attacker != &attackerTarget || g_handler.point != &point ||
            g_handler.info != &info || g_handlerOriginal != original) {
            return fail("handler arguments");
        }
        if (g_original.enemy != &fake || g_original.attacker != &attackerTarget || g_original.point != &point ||
            g_original.info != &info) {
            return fail("original arguments");
        }
    }

    g_suppress = true;
    if (callSlot(thunked, &fake, &attackerTarget, &point, &info) != 0) return fail("stack unbalanced (suppressed)");
    if (g_handler.calls != kPassCount + 1 || g_original.calls != kPassCount) return fail("suppression ran the original");

    std::printf("PASS\n");
    return 0;
}
