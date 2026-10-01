// version.dll proxy: every export forwards to the system DLL (no code of ours runs for them); DllMain starts the
// adapter on its own thread when the host process is the game.
#include <windows.h>

#include <cwchar>

#include "init.h"

namespace {

// The crash reporter (crs-handler.exe) in the game folder loads this DLL too; only the game runs the adapter.
constexpr const wchar_t* kGameExe = L"DS2.exe";

bool hostIsGame() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    return _wcsicmp(slash ? slash + 1 : path, kGameExe) == 0;
}

}  // namespace

#define SYSTEM_VERSION_DLL "\\\\.\\GLOBALROOT\\SystemRoot\\System32\\version.dll"
#define FORWARD(name) __pragma(comment(linker, "/EXPORT:" #name "=" SYSTEM_VERSION_DLL "." #name))

FORWARD(GetFileVersionInfoA)
FORWARD(GetFileVersionInfoByHandle)
FORWARD(GetFileVersionInfoExA)
FORWARD(GetFileVersionInfoExW)
FORWARD(GetFileVersionInfoSizeA)
FORWARD(GetFileVersionInfoSizeExA)
FORWARD(GetFileVersionInfoSizeExW)
FORWARD(GetFileVersionInfoSizeW)
FORWARD(GetFileVersionInfoW)
FORWARD(VerFindFileA)
FORWARD(VerFindFileW)
FORWARD(VerInstallFileA)
FORWARD(VerInstallFileW)
FORWARD(VerLanguageNameA)
FORWARD(VerLanguageNameW)
FORWARD(VerQueryValueA)
FORWARD(VerQueryValueW)

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
