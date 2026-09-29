#include "d3d9_hook.h"

#include <windows.h>

#include "debug_stats.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr size_t kCreateDeviceSlot = 16;
constexpr size_t kResetSlot = 16;
constexpr size_t kEndSceneSlot = 42;
constexpr char kD3d9Module[] = "d3d9.dll";
constexpr char kCreateExport[] = "Direct3DCreate9";

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
using EndSceneFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

d3d9_hook::Handlers g_handlers{};
Direct3DCreate9Fn g_originalCreate9 = nullptr;
CreateDeviceFn g_originalCreateDevice = nullptr;
EndSceneFn g_originalEndScene = nullptr;
ResetFn g_originalReset = nullptr;
uintptr_t g_create9Address = 0;
uintptr_t g_createDeviceAddress = 0;
uintptr_t g_endSceneAddress = 0;
uintptr_t g_resetAddress = 0;

uintptr_t vtableEntry(void* object, size_t slot) { return reinterpret_cast<uintptr_t*>(*reinterpret_cast<void**>(object))[slot]; }

bool hookOnce(const char* name, uintptr_t address, uintptr_t& hooked, void* detour, void** original) {
    if (address == hooked) return true;
    if (!hooks::install(name, address, detour, original)) return false;
    hooked = address;
    return true;
}

HRESULT STDMETHODCALLTYPE endSceneDetour(IDirect3DDevice9* device) {
    if (g_handlers.onEndScene) g_handlers.onEndScene(device);
    return g_originalEndScene(device);
}

HRESULT STDMETHODCALLTYPE resetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    if (g_handlers.onBeforeReset) g_handlers.onBeforeReset();
    const HRESULT result = g_originalReset(device, params);
    if (SUCCEEDED(result) && g_handlers.onAfterReset) g_handlers.onAfterReset();
    return result;
}

HRESULT STDMETHODCALLTYPE createDeviceDetour(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                             D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device) {
    const HRESULT result = g_originalCreateDevice(d3d, adapter, type, focus, flags, params, device);
    if (SUCCEEDED(result) && device && *device) {
        logger::write("d3d9_hook: game device created");
        hookOnce("IDirect3DDevice9::EndScene", vtableEntry(*device, kEndSceneSlot), g_endSceneAddress,
                 reinterpret_cast<void*>(&endSceneDetour), reinterpret_cast<void**>(&g_originalEndScene));
        hookOnce("IDirect3DDevice9::Reset", vtableEntry(*device, kResetSlot), g_resetAddress,
                 reinterpret_cast<void*>(&resetDetour), reinterpret_cast<void**>(&g_originalReset));
    }
    return result;
}

IDirect3D9* WINAPI create9Detour(UINT sdkVersion) {
    IDirect3D9* d3d = g_originalCreate9(sdkVersion);
    if (d3d) {
        hookOnce("IDirect3D9::CreateDevice", vtableEntry(d3d, kCreateDeviceSlot), g_createDeviceAddress,
                 reinterpret_cast<void*>(&createDeviceDetour), reinterpret_cast<void**>(&g_originalCreateDevice));
    }
    return d3d;
}

}  // namespace

namespace d3d9_hook {

bool install(const Handlers& handlers) {
    HMODULE d3d9 = LoadLibraryA(kD3d9Module);  // loaded by the game anyway; this only makes sure it is mapped now
    const auto create9 = d3d9 ? reinterpret_cast<uintptr_t>(GetProcAddress(d3d9, kCreateExport)) : 0;
    if (!create9) {
        debug_stats::setError("d3d9: Direct3DCreate9 not found");
        logger::write("d3d9_hook: Direct3DCreate9 not found in d3d9.dll");
        return false;
    }
    g_handlers = handlers;
    if (!hookOnce("Direct3DCreate9", create9, g_create9Address, reinterpret_cast<void*>(&create9Detour),
                  reinterpret_cast<void**>(&g_originalCreate9))) {
        debug_stats::setError("d3d9: Direct3DCreate9 hook failed");
        return false;
    }
    return true;
}

void uninstall() {
    for (const uintptr_t address : {g_create9Address, g_createDeviceAddress, g_endSceneAddress, g_resetAddress}) {
        if (address) hooks::remove(address);
    }
}

}  // namespace d3d9_hook
