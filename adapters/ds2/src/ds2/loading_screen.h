#pragma once
// DS2-internal: whether the game's loading screen is up. The loading screen's menu function (DSUILoadingScreenMenuFunction)
// exists exactly while it is shown, so its constructor and destructor are hooked. A load or a fast travel keeps the
// player's state machine running behind it, so "in gameplay" alone does not tell that the world has finished loading.
#include <chrono>

namespace loading_screen {

// Start-up, before the world loads.
void installEarly();

// Whether the loading screen is up. Any thread.
bool shown();

// Whether it has been gone for at least `period` (true from the start if it was never shown). Any thread.
bool goneFor(std::chrono::milliseconds period);

}  // namespace loading_screen
