// Checks the generated forwarding thunks of remote_storage_proxy against a fake 64-slot interface.
// Exit 0 when every slot forwards with the real `this` and its stack arguments, and overrides receive the proxy.
#include <windows.h>

#include <cstdio>
#include <utility>

#include "../src/remote_storage_proxy.h"

namespace {

using remote_storage_proxy::kSlotCount;
using remote_storage_proxy::Proxy;

constexpr int kOverriddenSlot = 5;
constexpr int kOverrideResult = 4242;
constexpr int kSlotResultBase = 1000;
constexpr int kArgA = 7;
constexpr int kArgB = 11;

struct Fake {
    void** vtable;
    void* lastThis[kSlotCount];
};

// Slot i returns kSlotResultBase + i + a + b and records `this`.
template <int I>
int __fastcall fakeSlot(Fake* self, void*, int a, int b) {
    self->lastThis[I] = self;
    return kSlotResultBase + I + a + b;
}

template <int... Is>
void fill(void** table, std::integer_sequence<int, Is...>) {
    ((table[Is] = reinterpret_cast<void*>(fakeSlot<Is>)), ...);
}

int __fastcall overrideSlot(Proxy*, void*, int a, int b) { return kOverrideResult + a + b; }

using CallFn = int(__fastcall*)(void*, void*, int, int);

int failures = 0;

void expect(bool ok, const char* what, int slot) {
    if (ok) return;
    std::printf("FAIL %s slot %d\n", what, slot);
    ++failures;
}

}  // namespace

int main() {
    void* table[kSlotCount];
    fill(table, std::make_integer_sequence<int, static_cast<int>(kSlotCount)>{});
    Fake fake{table, {}};

    remote_storage_proxy::Overrides overrides{};
    overrides[kOverriddenSlot] = reinterpret_cast<void*>(overrideSlot);

    Proxy* proxy = remote_storage_proxy::get(&fake, overrides);
    expect(proxy != nullptr, "proxy created", -1);
    if (!proxy) return 1;
    expect(remote_storage_proxy::get(&fake, overrides) == proxy, "cached by pointer", -1);

    for (int i = 0; i < static_cast<int>(kSlotCount); ++i) {
        const int result = reinterpret_cast<CallFn>(proxy->vtable[i])(proxy, nullptr, kArgA, kArgB);
        if (i == kOverriddenSlot) {
            expect(result == kOverrideResult + kArgA + kArgB, "override result", i);
            expect(fake.lastThis[i] == nullptr, "override does not reach the real slot", i);
        } else {
            expect(result == kSlotResultBase + i + kArgA + kArgB, "forwarded result", i);
            expect(fake.lastThis[i] == &fake, "forwarded this is the real interface", i);
        }
    }
    std::printf(failures ? "proxy_test FAILED (%d)\n" : "proxy_test OK\n", failures);
    return failures ? 1 : 0;
}
