#pragma once
#include <cstdint>

// The "Co-op" label of the title menu's fifth row (title_coop.h). The row's label is a cell of the title's DXT5 atlas
// texture (a managed D3D9 texture, so its memory copy can be locked); the label is drawn into that cell in memory, no
// game file changes. The game thread finds and checks the texture, the render thread writes it.
namespace title_label {

// Game thread, when the title's main menu opens: finds the atlas the title requested and queues it for drawing when
// it is the texture the cell rules expect (512 x 512 DXT5, one level, managed).
void publish(uintptr_t title);

// Render thread, every frame before the overlay draws: draws the label into a queued atlas.
void applyPending();

}  // namespace title_label
