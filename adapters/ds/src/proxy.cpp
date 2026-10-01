// version.dll proxy: every export forwards to the system DLL (no code of ours runs for them); DllMain starts the
// adapter on its own thread.
#include <windows.h>

#include "init.h"

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
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // The adapter threads must never outlive the image, so the DLL stays mapped for the process lifetime.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        if (HANDLE thread = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
