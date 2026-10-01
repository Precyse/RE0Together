#include "dx12_hook.h"

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <chrono>

#include "hooks.h"
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "log.h"

namespace {

// COM vtable slots (declaration order in d3d12.h / dxgi1_2.h).
constexpr size_t kFactoryCreateSwapChainSlot = 10;
constexpr size_t kFactoryCreateSwapChainForHwndSlot = 15;
constexpr size_t kQueueExecuteCommandListsSlot = 10;
constexpr size_t kSwapChainPresentSlot = 8;
constexpr size_t kSwapChainPresent1Slot = 22;

constexpr UINT kMaxBackBuffers = 8;
constexpr UINT kSrvCapacity = 64;
constexpr UINT kDummyBufferCount = 2;
constexpr DWORD kFenceWaitMs = 100;
constexpr float kMaxDeltaSeconds = 0.25f;

using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                            IDXGISwapChain1**);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

CreateSwapChainFn g_createSwapChain = nullptr;
CreateSwapChainForHwndFn g_createSwapChainForHwnd = nullptr;
ExecuteCommandListsFn g_executeCommandLists = nullptr;
PresentFn g_present = nullptr;
Present1Fn g_present1 = nullptr;

dx12_hook::DrawFn g_draw = nullptr;
std::atomic<ID3D12CommandQueue*> g_swapChainQueue{nullptr};  // the queue the game created its swap chain on
std::atomic<ID3D12CommandQueue*> g_seenQueue{nullptr};       // fallback: a direct queue seen executing
std::atomic<bool> g_disabled{false};
thread_local bool t_drawing = false;  // Present1 and Present may call each other inside DXGI: draw once per frame

// Everything the overlay owns on the game's device; built on the first Present, render thread only.
struct Renderer {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    ID3D12CommandAllocator* allocators[kMaxBackBuffers] = {};
    UINT64 allocatorFence[kMaxBackBuffers] = {};
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = nullptr;
    UINT rtvStride = 0;
    UINT srvUsed = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool ready = false;
    std::chrono::steady_clock::time_point lastFrame;
};
Renderer g_renderer;

void** vtableOf(void* object) { return *reinterpret_cast<void***>(object); }

void allocateSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    Renderer& r = g_renderer;
    const UINT stride = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT index = r.srvUsed < kSrvCapacity ? r.srvUsed++ : kSrvCapacity - 1;
    *cpu = r.srvHeap->GetCPUDescriptorHandleForHeapStart();
    *gpu = r.srvHeap->GetGPUDescriptorHandleForHeapStart();
    cpu->ptr += static_cast<SIZE_T>(index) * stride;
    gpu->ptr += static_cast<UINT64>(index) * stride;
}

void freeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

bool buildRenderer(IDXGISwapChain3* swapChain, ID3D12CommandQueue* queue, DXGI_FORMAT format, UINT bufferCount) {
    Renderer& r = g_renderer;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&r.device)))) return false;
    r.queue = queue;
    r.format = format;

    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxBackBuffers, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCapacity,
                                   D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(r.device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&r.rtvHeap))) ||
        FAILED(r.device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&r.srvHeap)))) {
        return false;
    }
    r.rtvStride = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (ID3D12CommandAllocator*& allocator : r.allocators) {
        if (FAILED(r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) return false;
    }
    if (FAILED(r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r.allocators[0], nullptr,
                                           IID_PPV_ARGS(&r.list))) ||
        FAILED(r.list->Close()) || FAILED(r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence)))) {
        return false;
    }
    r.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui_ImplDX12_InitInfo info;
    info.Device = r.device;
    info.CommandQueue = queue;
    info.NumFramesInFlight = static_cast<int>(bufferCount);
    info.RTVFormat = format;
    info.SrvDescriptorHeap = r.srvHeap;
    info.SrvDescriptorAllocFn = allocateSrv;
    info.SrvDescriptorFreeFn = freeSrv;
    if (!ImGui_ImplDX12_Init(&info)) return false;
    r.lastFrame = std::chrono::steady_clock::now();
    r.ready = true;
    logger::write("dx12: overlay ready (format %d, %u buffers)", static_cast<int>(format), bufferCount);
    return true;
}

void waitForAllocator(UINT index) {
    Renderer& r = g_renderer;
    if (r.fence->GetCompletedValue() >= r.allocatorFence[index]) return;
    r.fence->SetEventOnCompletion(r.allocatorFence[index], r.fenceEvent);
    WaitForSingleObject(r.fenceEvent, kFenceWaitMs);
}

void renderFrame(IDXGISwapChain* chain) {
    ID3D12CommandQueue* queue = g_swapChainQueue.load();
    if (!queue) queue = g_seenQueue.load();
    if (!queue) return;
    IDXGISwapChain3* swapChain = nullptr;
    if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&swapChain)))) return;
    DXGI_SWAP_CHAIN_DESC desc{};
    swapChain->GetDesc(&desc);
    Renderer& r = g_renderer;
    if (!r.ready && !buildRenderer(swapChain, queue, desc.BufferDesc.Format, desc.BufferCount)) {
        logger::write("dx12: overlay setup failed, drawing disabled");
        g_disabled = true;
        swapChain->Release();
        return;
    }
    const UINT index = swapChain->GetCurrentBackBufferIndex();
    ID3D12Resource* backBuffer = nullptr;
    if (index >= kMaxBackBuffers || desc.BufferDesc.Format != r.format ||
        FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer)))) {
        swapChain->Release();
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = r.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * r.rtvStride;
    r.device->CreateRenderTargetView(backBuffer, nullptr, rtv);

    const auto now = std::chrono::steady_clock::now();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height));
    const float delta = std::chrono::duration<float>(now - r.lastFrame).count();
    io.DeltaTime = delta > 0 && delta < kMaxDeltaSeconds ? delta : kMaxDeltaSeconds;
    r.lastFrame = now;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    g_draw(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Render();

    waitForAllocator(index);
    r.allocators[index]->Reset();
    r.list->Reset(r.allocators[index], nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    r.list->ResourceBarrier(1, &barrier);
    r.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    r.list->SetDescriptorHeaps(1, &r.srvHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), r.list);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    r.list->ResourceBarrier(1, &barrier);
    r.list->Close();
    ID3D12CommandList* lists[] = {r.list};
    g_executeCommandLists(r.queue, 1, lists);
    r.queue->Signal(r.fence, ++r.fenceValue);
    r.allocatorFence[index] = r.fenceValue;
    backBuffer->Release();
    swapChain->Release();
}

void guardedRender(IDXGISwapChain* chain) {
    if (g_disabled.load()) return;
    __try {
        renderFrame(chain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
        logger::write("dx12: fault 0x%08lx while drawing, overlay disabled", GetExceptionCode());
    }
}

void drawOnce(IDXGISwapChain* chain, UINT flags) {
    if (t_drawing || (flags & DXGI_PRESENT_TEST)) return;
    t_drawing = true;
    guardedRender(chain);
}

void rememberSwapChainQueue(IUnknown* device) {
    ID3D12CommandQueue* queue = nullptr;
    if (device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue)))) {
        g_swapChainQueue = queue;  // the reference is kept: the queue lives as long as the swap chain
        logger::write("dx12: swap chain created on queue %p", static_cast<void*>(queue));
    }
}

HRESULT STDMETHODCALLTYPE createSwapChainDetour(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                                IDXGISwapChain** out) {
    rememberSwapChainQueue(device);
    return g_createSwapChain(factory, device, desc, out);
}

HRESULT STDMETHODCALLTYPE createSwapChainForHwndDetour(IDXGIFactory2* factory, IUnknown* device, HWND window,
                                                       const DXGI_SWAP_CHAIN_DESC1* desc,
                                                       const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                                       IDXGIOutput* output, IDXGISwapChain1** out) {
    rememberSwapChainQueue(device);
    return g_createSwapChainForHwnd(factory, device, window, desc, fullscreen, output, out);
}

void STDMETHODCALLTYPE executeCommandListsDetour(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    if (!g_seenQueue.load() && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_seenQueue = queue;
    g_executeCommandLists(queue, count, lists);
}

HRESULT STDMETHODCALLTYPE presentDetour(IDXGISwapChain* chain, UINT sync, UINT flags) {
    const bool outer = !t_drawing;
    drawOnce(chain, flags);
    const HRESULT result = g_present(chain, sync, flags);
    if (outer) t_drawing = false;
    return result;
}

HRESULT STDMETHODCALLTYPE present1Detour(IDXGISwapChain1* chain, UINT sync, UINT flags,
                                         const DXGI_PRESENT_PARAMETERS* params) {
    const bool outer = !t_drawing;
    drawOnce(chain, flags);
    const HRESULT result = g_present1(chain, sync, flags, params);
    if (outer) t_drawing = false;
    return result;
}

// Vtable entries of throwaway DXGI/D3D12 objects; the implementations are shared with the game's objects.
struct DummyVtables {
    void* createSwapChain = nullptr;
    void* createSwapChainForHwnd = nullptr;
    void* executeCommandLists = nullptr;
    void* present = nullptr;
    void* present1 = nullptr;
};

bool readDummyVtables(DummyVtables& out) {
    WNDCLASSEXW wc{sizeof(wc), 0, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
                   L"coop_dx12_probe", nullptr};
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* swapChain = nullptr;
    bool ok = window && SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) &&
              SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    if (ok) {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        ok = SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    }
    if (ok) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kDummyBufferCount;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ok = SUCCEEDED(factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr, &swapChain));
    }
    if (ok) {
        out.createSwapChain = vtableOf(factory)[kFactoryCreateSwapChainSlot];
        out.createSwapChainForHwnd = vtableOf(factory)[kFactoryCreateSwapChainForHwndSlot];
        out.executeCommandLists = vtableOf(queue)[kQueueExecuteCommandListsSlot];
        out.present = vtableOf(swapChain)[kSwapChainPresentSlot];
        out.present1 = vtableOf(swapChain)[kSwapChainPresent1Slot];
    }
    if (swapChain) swapChain->Release();
    if (queue) queue->Release();
    if (device) device->Release();
    if (factory) factory->Release();
    if (window) DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

bool hook(const char* name, void* target, void* detour, void** original) {
    return hooks::install(name, reinterpret_cast<uintptr_t>(target), detour, original);
}

}  // namespace

namespace dx12_hook {

bool install(DrawFn draw) {
    g_draw = draw;
    DummyVtables v;
    if (!readDummyVtables(v)) {
        logger::write("dx12: could not create the probe device, overlay disabled");
        return false;
    }
    return hook("IDXGIFactory::CreateSwapChain", v.createSwapChain, reinterpret_cast<void*>(&createSwapChainDetour),
                reinterpret_cast<void**>(&g_createSwapChain)) &&
           hook("IDXGIFactory2::CreateSwapChainForHwnd", v.createSwapChainForHwnd,
                reinterpret_cast<void*>(&createSwapChainForHwndDetour), reinterpret_cast<void**>(&g_createSwapChainForHwnd)) &&
           hook("ID3D12CommandQueue::ExecuteCommandLists", v.executeCommandLists,
                reinterpret_cast<void*>(&executeCommandListsDetour), reinterpret_cast<void**>(&g_executeCommandLists)) &&
           hook("IDXGISwapChain::Present", v.present, reinterpret_cast<void*>(&presentDetour),
                reinterpret_cast<void**>(&g_present)) &&
           hook("IDXGISwapChain1::Present1", v.present1, reinterpret_cast<void*>(&present1Detour),
                reinterpret_cast<void**>(&g_present1));
}

}  // namespace dx12_hook
