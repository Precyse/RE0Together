#include "virtual_keys.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstring>

#include "hooks.h"
#include "log.h"

namespace {

// COM vtable slots: IDirectInput8::CreateDevice and IDirectInputDevice8::GetDeviceState
constexpr size_t kCreateDeviceSlot = 3;
constexpr size_t kGetDeviceStateSlot = 9;
constexpr DWORD kKeyboardStateSize = 256;
constexpr uint8_t kKeyDown = 0x80;
constexpr unsigned kByteBits = 8;
constexpr unsigned kWordBits = 32;
constexpr int kTapReads = 6;  // keyboard reads a tap stays down (the game reads once or twice per frame)

// GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000}
constexpr GUID kSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using CreateDeviceFn = HRESULT(__stdcall*)(void* self, REFGUID guid, void** device, void* outer);
using GetDeviceStateFn = HRESULT(__stdcall*)(void* self, DWORD size, void* data);

CreateDeviceFn g_originalCreateDevice = nullptr;
GetDeviceStateFn g_originalGetDeviceState = nullptr;
std::atomic<bool> g_createHooked{false};
std::atomic<bool> g_stateHooked{false};

std::atomic<uint8_t> g_tapKey{0};
std::atomic<int> g_tapReadsLeft{0};
std::atomic<bool> g_muted{false};
std::atomic<uint16_t> g_hiddenKeys{0};  // two DIK scan codes, low byte first
std::array<std::atomic<uint32_t>, kKeyboardStateSize / kWordBits> g_realDown{};  // one bit per DIK code, last read

// Stores the real keys of a read (all up when it failed: the game lost the focus).
void recordReal(const uint8_t* keys) {
    for (size_t word = 0; word < g_realDown.size(); ++word) {
        uint32_t bits = 0;
        for (unsigned bit = 0; keys && bit < kWordBits; ++bit) {
            if (keys[word * kWordBits + bit] & kKeyDown) bits |= 1u << bit;
        }
        g_realDown[word] = bits;
    }
}

uintptr_t vtableSlot(void* object, size_t slot) { return reinterpret_cast<uintptr_t>((*static_cast<void***>(object))[slot]); }

HRESULT __stdcall getDeviceStateDetour(void* self, DWORD size, void* data) {
    const HRESULT result = g_originalGetDeviceState(self, size, data);
    if (size != kKeyboardStateSize || !data) return result;
    auto* keys = static_cast<uint8_t*>(data);
    recordReal(SUCCEEDED(result) ? keys : nullptr);
    if (FAILED(result)) return result;
    if (g_muted) std::memset(keys, 0, kKeyboardStateSize);
    for (uint16_t hidden = g_hiddenKeys; hidden != 0; hidden >>= kByteBits) {
        keys[hidden & 0xFF] = 0;
    }
    if (g_tapReadsLeft > 0) {
        keys[g_tapKey.load()] = kKeyDown;
        --g_tapReadsLeft;
    }
    return result;
}

HRESULT __stdcall createDeviceDetour(void* self, REFGUID guid, void** device, void* outer) {
    const HRESULT result = g_originalCreateDevice(self, guid, device, outer);
    if (SUCCEEDED(result) && device && *device && IsEqualGUID(guid, kSysKeyboard) && !g_stateHooked.exchange(true)) {
        hooks::install("IDirectInputDevice8::GetDeviceState", vtableSlot(*device, kGetDeviceStateSlot),
                       reinterpret_cast<void*>(&getDeviceStateDetour), reinterpret_cast<void**>(&g_originalGetDeviceState));
    }
    return result;
}

}  // namespace

namespace virtual_keys {

void onDirectInput(void* directInput) {
    if (!directInput || g_createHooked.exchange(true)) return;
    hooks::install("IDirectInput8::CreateDevice", vtableSlot(directInput, kCreateDeviceSlot),
                   reinterpret_cast<void*>(&createDeviceDetour), reinterpret_cast<void**>(&g_originalCreateDevice));
}

void tap(uint8_t scancode) {
    g_tapKey = scancode;
    g_tapReadsLeft = kTapReads;
}

bool realKeyDown(uint8_t scancode) { return (g_realDown[scancode / kWordBits] >> (scancode % kWordBits)) & 1u; }

void setHiddenKeys(const HiddenKeys& scancodes) {
    const auto packed = static_cast<uint16_t>(scancodes[0] | scancodes[1] << kByteBits);
    if (g_hiddenKeys.exchange(packed) != packed) {
        logger::write("virtual_keys: keys 0x%02x 0x%02x hidden from the game", scancodes[0], scancodes[1]);
    }
}

void setRealKeyboardMuted(bool muted) {
    if (g_muted.exchange(muted) != muted) logger::write("virtual_keys: real keyboard %s", muted ? "muted" : "restored");
}

}  // namespace virtual_keys
