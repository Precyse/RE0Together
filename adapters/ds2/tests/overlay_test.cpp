// Real windowed D3D12 swap chain created after dx12_hook::install (like the game's): presents frames and checks
// the hook learned the swap chain's queue and ran the draw callback every frame without disabling itself. Then the
// swap chain is recreated on a new queue in another format (as the game does when it loses focus): drawing must move
// to the new queue and the device must stay alive.
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>

#include "dx12_hook.h"
#include "imgui.h"

namespace {

constexpr UINT kWidth = 640, kHeight = 360;
constexpr UINT kBufferCount = 3;
constexpr int kFrames = 120;

int g_draws = 0;

void draw(float width, float height) {
    ++g_draws;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(width * 0.25f, height * 0.25f),
                                                  ImVec2(width * 0.75f, height * 0.75f), IM_COL32(200, 200, 200, 255));
}

}  // namespace

int main() {
    if (!dx12_hook::install(draw)) {
        std::printf("SKIP: no D3D12 device\n");
        return 0;
    }
    WNDCLASSEXW wc{sizeof(wc), 0, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
                   L"overlay_test", nullptr};
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"overlay_test", WS_OVERLAPPEDWINDOW, 0, 0, kWidth, kHeight,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* swapChain = nullptr;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr, &swapChain))) {
        std::printf("FAIL: swap chain\n");
        return 1;
    }
    for (int i = 0; i < kFrames; ++i) swapChain->Present(1, 0);
    if (device->GetDeviceRemovedReason() != S_OK) {
        std::printf("FAIL: device removed after the first swap chain (0x%08lx)\n",
                    static_cast<unsigned long>(device->GetDeviceRemovedReason()));
        return 1;
    }

    // Like a game: let the queue drain (the overlay's work on it included) before releasing the swap chain.
    ID3D12Fence* flush = nullptr;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&flush));
    queue->Signal(flush, 1);
    flush->SetEventOnCompletion(1, nullptr);
    flush->Release();
    swapChain->Release();
    queue->Release();
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferCount = kBufferCount + 1;
    HWND second = CreateWindowExW(0, wc.lpszClassName, L"overlay_test 2", WS_OVERLAPPEDWINDOW, 0, 0, kWidth, kHeight,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    const HRESULT recreated = factory->CreateSwapChainForHwnd(queue, second, &desc, nullptr, nullptr, &swapChain);
    if (FAILED(recreated)) {
        std::printf("FAIL: second swap chain 0x%08lx (device 0x%08lx)\n", static_cast<unsigned long>(recreated),
                    static_cast<unsigned long>(device->GetDeviceRemovedReason()));
        return 1;
    }
    for (int i = 0; i < kFrames; ++i) swapChain->Present(1, 0);
    const bool deviceAlive = device->GetDeviceRemovedReason() == S_OK;
    const bool ok = g_draws == 2 * kFrames && deviceAlive;
    std::printf("%s: %d of %d frames drawn across a swap chain recreation, device %s\n", ok ? "PASS" : "FAIL",
                g_draws, 2 * kFrames, deviceAlive ? "alive" : "removed");
    return ok ? 0 : 1;
}
