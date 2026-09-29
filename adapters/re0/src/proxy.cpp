// dinput8.dll proxy: forwards DirectInput8Create to the system DLL and starts the adapter.
#include <windows.h>
#include <unknwn.h>

#include <mutex>
#include <string>

#include "init.h"

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

constexpr const char* kRealExport = "DirectInput8Create";

std::once_flag g_loadOnce;
DirectInput8CreateFn g_real = nullptr;

std::wstring realDllPath() {
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemWow64DirectoryW(dir, MAX_PATH);
    if (n == 0) n = GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir, n) + L"\\dinput8.dll";
}

void loadReal() {
    const HMODULE module = LoadLibraryW(realDllPath().c_str());
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
        // The net thread must never outlive the image, so the DLL stays mapped for the process lifetime.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&DirectInput8Create), &pinned);
        if (HANDLE thread = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr)) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        shutdownAdapter();
    }
    return TRUE;
}
