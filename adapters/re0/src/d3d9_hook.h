#pragma once
#include <d3d9.h>

// Finds the game's real device as it is created: inline-hooks d3d9.dll's Direct3DCreate9, wraps the returned
// IDirect3D9::CreateDevice, then hooks that device's EndScene and Reset. d3d9.dll is not encrypted, so this is
// installed first thing, before the game (still being unpacked by SteamStub) can create its device.
namespace d3d9_hook {

struct Handlers {
    void (*onEndScene)(IDirect3DDevice9* device);  // called before the game's EndScene runs
    void (*onBeforeReset)();                       // release device-bound resources
    void (*onAfterReset)();                        // called after a successful Reset
};

bool install(const Handlers& handlers);

void uninstall();

}  // namespace d3d9_hook
