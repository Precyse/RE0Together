#pragma once
// DS2-internal: log-only instruments for the BT catch on the host (adapter.ini diagnostics=1, never on in a shipped config).
// They answer whether the host's BTs act on the guest's body: every gazer catch anim event, every hunter cling anim event
// and the start and end of the catcher's annihilation with the player index it targets (0 = the local player, 1 = the
// remote body). Static basis: docs/DS2_NOTES.md, "BT and catcher sync".
#include <cstdint>

namespace bt_diag {

// Start-up: detours on the gazer catch event (0x1419cacf0) and the hunter cling event (0x141abf1d0); they log and call the
// original.
void installEarly();

// The host's catcher annihilation (0x141a05080) is starting (`begin`) or has returned. No-op unless installEarly ran.
void annihilation(uintptr_t component, bool begin);

}  // namespace bt_diag
