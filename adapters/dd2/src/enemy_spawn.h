#pragma once
// An enemy's generate record across machines (DD2 design world.md 1 "Generation on guests", probe P7-2): the host turns
// a live enemy's generate container into the engine's own save record (convContainerToRequestSaveData) and sends it as
// reflection JSON; a guest turns it back into a container (convRequestSaveDataToContainer, the save loader's path) and
// asks for the body. The record carries the request id, so the new body has the host's UniqueID and binds as a mirror.
// Game thread only; every refusal is logged with its step.
#include <optional>
#include <string>

namespace enemy_spawn {

// Reads the generate option literals; false when the type is missing (no creation then).
bool init();

// Host: the record of one of our enemies as JSON, or null.
std::optional<std::string> recordOf(void* character);

// Guest: generates an enemy from a host record; false when a step refused.
bool create(const std::string& json);

// Guest: removes one of our own enemies (its generate container through GenerateManager.requestDestroy).
bool destroy(void* character);

}  // namespace enemy_spawn
