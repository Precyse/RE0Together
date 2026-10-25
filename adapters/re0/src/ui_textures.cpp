#include "ui_textures.h"

#include <windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <map>
#include <vector>

#include "log.h"
#include "paths.h"

namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kBytesPerPixel = 4;

std::map<std::wstring, ui_textures::Image> g_images;  // render thread only

// The file as 32 bit BGRA pixels (straight alpha), tightly packed.
bool decode(const std::wstring& path, UINT& width, UINT& height, std::vector<BYTE>& pixels) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) {
        return false;
    }
    pixels.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * kBytesPerPixel, static_cast<UINT>(pixels.size()), pixels.data()));
}

IDirect3DTexture9* upload(IDirect3DDevice9* device, UINT width, UINT height, const std::vector<BYTE>& pixels) {
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr))) return nullptr;
    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        texture->Release();
        return nullptr;
    }
    for (UINT row = 0; row < height; ++row) {
        memcpy(static_cast<BYTE*>(locked.pBits) + static_cast<size_t>(row) * locked.Pitch, pixels.data() + static_cast<size_t>(row) * width * kBytesPerPixel,
               static_cast<size_t>(width) * kBytesPerPixel);
    }
    texture->UnlockRect(0);
    return texture;
}

// COM is initialised for the duration of a load: the render thread may not have done it (a thread that already did it in
// another mode keeps working).
ui_textures::Image load(IDirect3DDevice9* device, const std::wstring& relativePath) {
    ui_textures::Image image;
    const HRESULT initialised = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> pixels;
    if (decode(gameDirectory() + L"\\" + relativePath, width, height, pixels)) {
        image.texture = upload(device, width, height, pixels);
        image.width = static_cast<int>(width);
        image.height = static_cast<int>(height);
    }
    if (SUCCEEDED(initialised)) CoUninitialize();
    return image;
}

}  // namespace

namespace ui_textures {

const Image& get(IDirect3DDevice9* device, const std::wstring& relativePath) {
    const auto known = g_images.find(relativePath);
    if (known != g_images.end()) return known->second;
    Image image = load(device, relativePath);
    if (!image.texture) logger::write("ui_textures: %ls is not available, drawing without it", relativePath.c_str());
    return g_images.emplace(relativePath, image).first->second;
}

void release() {
    for (auto& [path, image] : g_images) {
        if (image.texture) image.texture->Release();
    }
    g_images.clear();
}

}  // namespace ui_textures
