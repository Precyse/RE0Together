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

UINT WINAPI getRawInputDataDetour(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    const UINT result = g_original(input, command, data, size, headerSize);
    if (command != RID_INPUT || !data || result == static_cast<UINT>(-1) || result < sizeof(RAWINPUT)) return result;
    auto* raw = static_cast<RAWINPUT*>(data);
    if (raw->header.dwType != RIM_TYPEKEYBOARD) return result;
    RAWKEYBOARD& key = raw->data.keyboard;
    if ((key.Flags & RI_KEY_BREAK) == 0 && g_claims(key.VKey)) {
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
