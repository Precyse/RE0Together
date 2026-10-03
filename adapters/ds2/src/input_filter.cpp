#include "input_filter.h"

#include <windows.h>

#include "import_patch.h"
#include "log.h"

namespace {

constexpr const char* kUserDll = "USER32.dll";
constexpr USHORT kUnknownKey = 0xFF;  // KEYBOARD_OVERRUN_MAKE_CODE, and no virtual key

using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

GetRawInputDataFn g_original = nullptr;
input_filter::KeyFilter g_claims = nullptr;
bool g_claimed[256] = {};  // keys whose press was claimed: their release is claimed too (the window thread only)

UINT WINAPI getRawInputDataDetour(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    const UINT result = g_original(input, command, data, size, headerSize);
    if (command != RID_INPUT || !data || result == static_cast<UINT>(-1) || result < sizeof(RAWINPUT)) return result;
    auto* raw = static_cast<RAWINPUT*>(data);
    if (raw->header.dwType != RIM_TYPEKEYBOARD) return result;
    RAWKEYBOARD& key = raw->data.keyboard;
    const bool release = (key.Flags & RI_KEY_BREAK) != 0;
    const bool known = key.VKey < sizeof(g_claimed);
    const bool claim = release ? known && g_claimed[key.VKey] : g_claims(key.VKey);
    if (known && !release) g_claimed[key.VKey] = claim;  // kept through the release (the game may read an event twice)
    if (claim) {
        key.MakeCode = kUnknownKey;
        key.VKey = kUnknownKey;
    }
    return result;
}

}  // namespace

namespace input_filter {

void install(KeyFilter claims) {
    g_claims = claims;
    // Set before the slot changes: the game's window thread may call the detour at once.
    g_original = reinterpret_cast<GetRawInputDataFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetRawInputData"));
    const bool redirected =
        g_original && import_patch::redirect(kUserDll, "GetRawInputData", reinterpret_cast<void*>(&getRawInputDataDetour));
    logger::write("input_filter: GetRawInputData %s", redirected ? "redirected" : "not imported");
}

}  // namespace input_filter
