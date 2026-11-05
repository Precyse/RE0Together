// winmm.dll proxy: every export forwards to the system DLL (no code of ours runs for them); DllMain starts the adapter
// on its own thread when the host process is the game.
#include <windows.h>

#include <cwchar>

#include "init.h"

namespace {

// crashpad_handler.exe and pers.exe in bin64 can load this DLL too; only the game runs the adapter.
constexpr const wchar_t* kGameExe = L"CrimsonDesert.exe";

bool hostIsGame() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    return _wcsicmp(slash ? slash + 1 : path, kGameExe) == 0;
}

}  // namespace

#define SYSTEM_WINMM_DLL "\\\\.\\GLOBALROOT\\SystemRoot\\System32\\winmm.dll"
#define FORWARD(name) __pragma(comment(linker, "/EXPORT:" #name "=" SYSTEM_WINMM_DLL "." #name))
#include "winmm_exports.inc"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && hostIsGame()) {
        DisableThreadLibraryCalls(module);
        // The adapter threads must never outlive the image, so the DLL stays mapped for the process lifetime.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        if (HANDLE thread = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
