#pragma once

// Keeps chosen keys from the game while an adapter menu uses them. DEATH STRANDING 2 reads the keyboard through raw
// input (WM_INPUT + GetRawInputData), so the game's import of GetRawInputData is redirected: a key press the filter
// claims reaches the game as an unknown key. Releases always pass, so no game key is left held.
namespace input_filter {

// True when the adapter wants this virtual key for itself right now. Called on the game's window thread.
using KeyFilter = bool (*)(unsigned virtualKey);

void install(KeyFilter claims);

}  // namespace input_filter
