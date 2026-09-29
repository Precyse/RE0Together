// Installs the D3D9 overlay hooks, then creates a real windowed device through this exe's own (patched)
// Direct3DCreate9 import the way the game does, shows the panel, renders frames and one Reset, and checks that the
// hooks fire, the panel draws and nothing faults. Exit 0 also when no device can be created.
#include <windows.h>

#include <d3d9.h>

#include <cstdio>

#include "../src/debug_lines.h"
#include "../src/debug_overlay.h"
#include "../src/debug_stats.h"

namespace {

constexpr UINT kBackbufferSize = 64;
constexpr int kFrames = 3;
constexpr uint32_t kFakeCount = 5;
constexpr float kToastSeconds = 30.0f;
constexpr size_t kToastCount = 3;  // one more than this is queued to cover the stack limit

int fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}

D3DPRESENT_PARAMETERS presentParameters(HWND window) {
    D3DPRESENT_PARAMETERS params{};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.BackBufferFormat = D3DFMT_UNKNOWN;
    params.BackBufferWidth = kBackbufferSize;
    params.BackBufferHeight = kBackbufferSize;
    params.hDeviceWindow = window;
    return params;
}

IDirect3DDevice9* createDevice(IDirect3D9* d3d, HWND window) {
    for (const D3DDEVTYPE type : {D3DDEVTYPE_HAL, D3DDEVTYPE_NULLREF}) {
        D3DPRESENT_PARAMETERS params = presentParameters(window);
        IDirect3DDevice9* device = nullptr;
        if (SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, type, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params,
                                        &device))) {
            return device;
        }
    }
    return nullptr;
}

void renderFrame(IDirect3DDevice9* device) {
    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    device->BeginScene();
    device->EndScene();
    device->Present(nullptr, nullptr, nullptr, nullptr);
}

void fillFakeStats() {
    SessionSnapshot session;
    session.linked = true;
    session.localSlot = 1;
    session.hostSlot = 0;
    session.epoch = 2;
    session.peers.push_back({0, 1, "host", 23});
    debug_stats::setSession(session);
    debug_stats::count(debug_stats::Counter::PadSent, kFakeCount);
    debug_stats::set(debug_stats::Gauge::BillyOwner, 0);
    debug_stats::setError("fake error");
    debug_stats::noteCallbackDisabled("fake_callback");
}

int checkLines() {
    const auto lines = debug_lines::build(debug_stats::snapshot());
    bool linkUp = false;
    bool errorShown = false;
    for (const auto& line : lines) {
        linkUp |= line.label == L"link" && line.value == L"up" && !line.bad;
        errorShown |= line.label == L"error" && line.bad;
    }
    if (!linkUp) return fail("link line is not up");
    if (!errorShown) return fail("error line is not flagged");
    return 0;
}

}  // namespace

int main() {
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, kBackbufferSize, kBackbufferSize, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    fillFakeStats();
    if (!debug_overlay::install()) return fail("hook install");
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    IDirect3DDevice9* device = d3d ? createDevice(d3d, window) : nullptr;
    if (!device) {
        std::printf("SKIP: no D3D9 device\n");
        return 0;
    }

    debug_overlay::setVisible(false);
    for (int i = 0; i <= static_cast<int>(kToastCount); ++i) debug_overlay::toast("Co-op player joined: test", kToastSeconds);
    renderFrame(device);
    if (debug_overlay::framesDrawn() != 1) return fail("toast was not drawn with the panel hidden");
    if (debug_overlay::disabled()) return fail("overlay faulted on a toast");

    debug_overlay::setVisible(true);
    for (int i = 0; i < kFrames; ++i) renderFrame(device);
    if (debug_overlay::framesSeen() != kFrames + 1) return fail("EndScene hook did not fire once per frame");
    if (debug_overlay::framesDrawn() != kFrames + 1) return fail("panel was not drawn each frame");

    D3DPRESENT_PARAMETERS params = presentParameters(window);
    const HRESULT reset = device->Reset(&params);
    if (FAILED(reset)) {
        std::printf("Reset hr=0x%08lx\n", static_cast<unsigned long>(reset));
        return fail("Reset");
    }
    renderFrame(device);
    if (debug_overlay::framesSeen() != kFrames + 2) return fail("EndScene hook stopped after Reset");
    if (debug_overlay::framesDrawn() != kFrames + 2) return fail("panel stopped drawing after Reset");
    if (debug_overlay::disabled()) return fail("overlay faulted");
    if (const int result = checkLines()) return result;

    debug_overlay::uninstall();
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    std::printf("PASS\n");
    return 0;
}
