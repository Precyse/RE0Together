#pragma once
// The native SaveService singleton and its idle state, shared by the capture launch and the host's save notice.

namespace save_service {

constexpr const char* kType = "via.storage.saveService.SaveService";

// The singleton, or null before the game made it.
void* instance();

// The service's SaveState is IDLE (false when it is not, or the SaveService calls or the literal are missing).
bool idle(void* service);

// The SaveState enum has an IDLE literal.
bool idleKnown();

}  // namespace save_service
