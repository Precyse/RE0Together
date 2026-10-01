#pragma once

// Draws Dear ImGui over the game's DX12 frames: hooks swap chain creation (to learn the game's command queue),
// ExecuteCommandLists (fallback queue capture) and Present. The draw callback runs on the render thread between
// ImGui::NewFrame and ImGui::Render with the back buffer size in pixels. A fault disables drawing for the session.
namespace dx12_hook {

using DrawFn = void (*)(float width, float height);

// Call from a normal thread (not under the loader lock), before the game creates its swap chain if possible.
bool install(DrawFn draw);

}  // namespace dx12_hook
