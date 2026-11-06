#pragma once
// The capture launch (adapter.ini capture=<slot>, the launcher's "Reading your character" step): the game is started only to
// read a character, never played. Once the game shows its title menu with the SaveService idle for 3 s, the save of that
// slot is loaded into a scratch app.SaveDataManager.GameSaveData through the SaveService (cleanData, addDataClass,
// loadSingle; no gameplay), its context and item sections are handed to the profile capture (profile::captureFrom), and the
// outcome is written to coop\capture.result for the launcher: "ok" or "failed: <why>". The launcher then closes the game
// (the SaveService's data classes are not the game's own any more). Gives up after 60 s.
#include <cstdint>

namespace offline_capture {

// Reads the SaveService enum literals and arms the capture of `slot`; logged when a literal is missing (the capture then fails).
void enable(int slot);

// Game thread, every step.
void tick(uint64_t nowMs);

}  // namespace offline_capture
