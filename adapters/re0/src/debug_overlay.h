#pragma once
#include <cstdint>

// In-game status panel drawn with Dear ImGui (DX9 backend, no input) from inside IDirect3DDevice9::EndScene.
// Reads debug_stats only, never game memory. Nothing ImGui-related exists until the panel or a toast is first shown.
namespace debug_overlay {

// Installs the D3D9 device hooks. The panel starts hidden; F8 toggles it while the game window is focused.
bool install();

void uninstall();

// EndScene calls seen so far, drawn or not.
uint32_t framesSeen();

// Frames the panel was rendered.
uint32_t framesDrawn();

// True once an overlay call faulted; the overlay stays off for the rest of the session.
bool disabled();

void setVisible(bool visible);

// Shows a message top-center for `seconds` even while the panel is hidden. Callable from any thread.
void toast(const char* text, float seconds);

}  // namespace debug_overlay
