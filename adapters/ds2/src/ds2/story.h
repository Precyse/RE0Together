#pragma once
// DS2-internal: the hooks that report the host's story and replay it on a guest (see ds2/story.cpp).
namespace story {

// Start-up: detours on the mission request functions and appliers and on the section request.
void installEarly();

// Logs every mission id with its state (the test command missions.txt), for choosing one to replay.
void logMissions();

}  // namespace story
