// dinput8.dll proxy: forwards DirectInput8Create to the system DLL and starts the adapter.
#include <windows.h>
#include <unknwn.h>

#include <mutex>

#include "init.h"
#include "system_dll.h"

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

constexpr const char* kRealExport = "DirectInput8Create";

std::once_flag g_loadOnce;
DirectInput8CreateFn g_real = nullptr;

void loadReal() {
    const HMODULE module = LoadLibraryW(systemDllPath(L"dinput8.dll").c_str());
    if (module) g_real = reinterpret_cast<DirectInput8CreateFn>(GetProcAddress(module, kRealExport));
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out,
                                             LPUNKNOWN outer) {
    std::call_once(g_loadOnce, loadReal);
    if (!g_real) return E_FAIL;
    return g_real(instance, version, iid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // The hooks point into this image, so it stays mapped for the process lifetime.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&DirectInput8Create), &pinned);
        if (HANDLE thread = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
