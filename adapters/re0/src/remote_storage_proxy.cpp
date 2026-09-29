#include "remote_storage_proxy.h"

#include <windows.h>

#include <map>
#include <memory>
#include <mutex>

#include "thunk_emit.h"

namespace {

constexpr size_t kThunkSize = 16;
constexpr size_t kPointerSize = sizeof(uint32_t);

using remote_storage_proxy::kSlotCount;
using remote_storage_proxy::Proxy;

struct Entry {
    Proxy proxy;
    std::array<void*, kSlotCount> vtable;
};

std::mutex g_mutex;
std::map<void*, std::unique_ptr<Entry>> g_entries;

// mov ecx, real; jmp [real vtable slot]. Only ecx and the flags are touched, so any argument layout passes through.
void emitForwarder(uint8_t* p, void* real, const void* realSlot) {
    thunk::emit(p, {0xB9});
    thunk::emitAddress(p, real);
    thunk::emit(p, {0xFF, 0x25});
    thunk::emitAddress(p, realSlot);
}

std::unique_ptr<Entry> build(void* real, const remote_storage_proxy::Overrides& overrides) {
    auto* thunks = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kSlotCount * kThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!thunks) return nullptr;
    auto** realVtable = *static_cast<void***>(real);
    auto entry = std::make_unique<Entry>();
    for (size_t i = 0; i < kSlotCount; ++i) {
        uint8_t* thunkAddress = thunks + i * kThunkSize;
        emitForwarder(thunkAddress, real, reinterpret_cast<uint8_t*>(realVtable) + i * kPointerSize);
        entry->vtable[i] = overrides[i] ? overrides[i] : thunkAddress;
    }
    FlushInstructionCache(GetCurrentProcess(), thunks, kSlotCount * kThunkSize);
    entry->proxy = {entry->vtable.data(), real};
    return entry;
}

}  // namespace

namespace remote_storage_proxy {

Proxy* get(void* real, const Overrides& overrides) {
    std::lock_guard lock(g_mutex);
    auto& slot = g_entries[real];
    if (!slot) slot = build(real, overrides);
    return slot ? &slot->proxy : nullptr;
}

}  // namespace remote_storage_proxy
