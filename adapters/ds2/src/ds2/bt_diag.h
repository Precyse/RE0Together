#pragma once
// DS2-internal: log-only instruments for the BT catch on the host (adapter.ini diagnostics=1, never on in a shipped config).
// They answer whether the host's BTs act on the guest's body: every hunter cling anim event
// and the start and end of the catcher's annihilation with the player index it targets (0 = the local player, 1 = the
// remote body). The gazer catch event is logged by ds2/bt_grab, which owns that hook. Static basis: docs/DS2_NOTES.md,
// "BT and catcher sync".
#include <cstdint>
#include <string>

namespace bt_diag {

// Start-up: a detour on the hunter cling event (0x141abf1d0); it logs and calls the original.
void installEarly();

// The UUID of the entity a BT component belongs to (component +0x48) as text, "unreadable" when it cannot be read.
std::string ownerText(uintptr_t component);

// The host's catcher annihilation (0x141a05080) is starting (`begin`) or has returned. No-op unless installEarly ran.
void annihilation(uintptr_t component, bool begin);

}  // namespace bt_diag
