#pragma once
#include <array>
#include <cstddef>

// Proxy for a COM-style x86 interface (thiscall vtable): every slot forwards to the real interface through a
// generated thunk unless an override is given.
namespace remote_storage_proxy {

constexpr size_t kSlotCount = 64;

// Per slot: a __fastcall function taking (Proxy* self, edx, args...), or null to forward to the real interface.
using Overrides = std::array<void*, kSlotCount>;

struct Proxy {
    void** vtable;
    void* real;
};

// The proxy for `real`, built on first use and cached by pointer. Override functions receive the Proxy as `this`.
Proxy* get(void* real, const Overrides& overrides);

}  // namespace remote_storage_proxy
