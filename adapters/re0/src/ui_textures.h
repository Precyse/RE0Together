#pragma once
#include <d3d9.h>

#include <string>

// The overlay's pictures (coop\ui\*.png, see ui_art_files.h): decoded with WIC into managed-pool textures of the game's
// device, which survive a device Reset. The render thread only: textures are made on first use and released with the
// overlay's ImGui when the device goes away.
namespace ui_textures {

struct Image {
    IDirect3DTexture9* texture = nullptr;  // null when the file is missing or not a picture
    int width = 0;
    int height = 0;
};

// The picture at `relativePath` (below the game folder), loaded once; a file that failed is not tried again (logged once).
const Image& get(IDirect3DDevice9* device, const std::wstring& relativePath);

// Releases every texture.
void release();

}  // namespace ui_textures
